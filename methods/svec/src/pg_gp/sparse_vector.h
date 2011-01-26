/**
 * @file
 * \brief Persistent storage for the Sparse Vector Datatype
 *
 */

/*! 

@defgroup svec Sparse Vectors
@ingroup support

@about

This module implements a sparse vector data type named "svec", which 
gives compressed storage of sparse vectors with many duplicate elements.

When we use arrays of floating point numbers for various calculations, 
    we will sometimes have long runs of zeros (or some other default value). 
    This is common in applications like scientific computing, 
    retail optimization, and text processing. Each floating point number takes 
    8 bytes of storage in memory and/or disk, so saving those zeros is often 
    worthwhile. There are also many computations that can benefit from skipping
    over the zeros.

    To focus the discussion, consider, for example, the following 
    array of doubles stored as a Postgres/GP "float8[]" data type:

\code
      '{0, 33,...40,000 zeros..., 12, 22 }'::float8[].
\endcode

    This array would occupy slightly more than 320KB of memory/disk, most of 
    it zeros. Even if we were to exploit the null bitmap and store the zeros 
    as nulls, we would still end up with a 5KB null bitmap, which is still 
    not nearly as memory efficient as we'd like. Also, as we perform various 
    operations on the array, we'll often be doing work on 40,000 fields that 
    would turn out not to be important. 

    To solve the problems associated with the processing of sparse vectors 
    discussed above, we adopt a simple Run Length Encoding (RLE) scheme to 
    represent sparse vectors as pairs of count-value arrays. So, for example, 
    the array above would be represented as follows

\code
      '{1,1,40000,1,1}:{0,33,0,12,22}'::madlib.svec,
\endcode

    which says there is 1 occurrence of 0, followed by 1 occurrence of 33, 
    followed by 40,000 occurrences of 0, etc. In contrast to the naive 
    representations, we only need 5 integers and 5 floating point numbers
    to store the array. Further, it is easy to implement vector operations 
    that can take advantage of the RLE representation to make computations 
    faster. The module provides a library of such functions.

    The current version only supports sparse vectors of float8
    values. Future versions will support other base types.

@usage

    Syntax reference can be found in gp_svec.sql_in.

@examp

    We can input an array directly as an svec as follows: 
\code   
    testdb=# select '{1,1,40000,1,1}:{0,33,0,12,22}'::madlib.svec;
\endcode
    We can also cast an array into an svec:
\code
    testdb=# select ('{0,33,...40,000 zeros...,12,22}'::float8[])::madlib.svec;
\endcode
    We can use operations with svec type like <, >, *, **, /, =, +, SUM, etc, 
    and they have meanings associated with typical vector operations. For 
    example, the plus (+) operator adds each of the terms of two vectors having
    the same dimension together. 
\code
    testdb=# select ('{0,1,5}'::float8[]::madlib.svec + '{4,3,2}'::float8[]::madlib.svec)::float8[];
     float8  
    ---------
     {4,4,7}
\endcode

    Without the casting into float8[] at the end, we get:
\code
    testdb=# select '{0,1,5}'::float8[]::madlib.svec + '{4,3,2}'::float8[]::madlib.svec;
     ?column?  
    ----------
    {2,1}:{4,7}    	    	
\endcode

    A dot product (%*%) between the two vectors will result in a scalar 
    result of type float8. The dot product should be (0*4 + 1*3 + 5*2) = 13, 
    like this:
\code
    testdb=# select '{0,1,5}'::float8[]::madlib.svec %*% '{4,3,2}'::float8[]::madlib.svec;
     ?column? 
    ----------
        13
\endcode

    Special vector aggregate functions are also available. SUM is self 
    explanatory. VEC_COUNT_NONZERO evaluates the count of non-zero terms 
    in each column found in a set of n-dimensional svecs and returns an 
    svec with the counts. For instance, if we have the vectors {0,1,5},
    {10,0,3},{0,0,3},{0,1,0}, then executing the VEC_COUNT_NONZERO() aggregate
    function would result in {1,2,3}:

\code
    testdb=# create table list (a madlib.svec);
    testdb=# insert into list values ('{0,1,5}'::float8[]), ('{10,0,3}'::float8[]), ('{0,0,3}'::float8[]),('{0,1,0}'::float8[]);
 
    testdb=# select madlib.vec_count_nonzero(a)::float8[] from list;
    vec_count_nonzero 
    -----------------
        {1,2,3}
\endcode

    We do not use null bitmaps in the svec data type. A null value in an svec 
    is represented explicitly as an NVP (No Value Present) value. For example, 
    we have:
\code
    testdb=# select '{1,2,3}:{4,null,5}'::madlib.svec;
          svec        
    -------------------
     {1,2,3}:{4,NVP,5}

    testdb=# select '{1,2,3}:{4,null,5}'::madlib.svec + '{2,2,2}:{8,9,10}'::madlib.svec; 
             ?column?         
     --------------------------
      {1,2,1,2}:{12,NVP,14,15}
\endcode

    An element of an svec can be accessed using the svec_proj() function,
    which takes an svec and the index of the element desired.
\code
    testdb=# select madlib.svec_proj('{1,2,3}:{4,5,6}'::madlib.svec, 1) + madlib.svec_proj('{4,5,6}:{1,2,3}'::madlib.svec, 15);     
     ?column? 
    ----------
        7
\endcode

    A subvector of an svec can be accessed using the svec_subvec() function,
    which takes an svec and the start and end index of the subvector desired.
\code
    testdb=# select madlib.svec_subvec('{2,4,6}:{1,3,5}'::madlib.svec, 2, 11);
       svec_subvec   
    ----------------- 
     {1,4,5}:{1,3,5}
\endcode

    The elements/subvector of an svec can be changed using the function 
    svec_change(). It takes three arguments: an m-dimensional svec sv1, a
    start index j, and an n-dimensional svec sv2 such that j + n - 1 <= m,
    and returns an svec like sv1 but with the subvector sv1[j:j+n-1] 
    replaced by sv2. An example follows:
\code
    testdb=# select madlib.svec_change('{1,2,3}:{4,5,6}'::madlib.svec,3,'{2}:{3}'::madlib.svec);
         svec_change     
    ---------------------
     {1,1,2,2}:{4,5,3,6}
\endcode

    There are also higher-order functions for processing svecs. For example,
    the following is the corresponding function for lapply() in R.
\code
    testdb=# select madlib.svec_lapply('sqrt', '{1,2,3}:{4,5,6}'::madlib.svec);
                      svec_lapply                  
    -----------------------------------------------
     {1,2,3}:{2,2.23606797749979,2.44948974278318}
\endcode

    The full list of functions available for operating on svecs are available
    in gp_svec.sql.

    Other examples of svecs usage can be found in the k-means module.

*/

#ifndef SPARSEVECTOR_H
#define SPARSEVECTOR_H

#include "SparseData.h"
#include "float_specials.h"

/**
 * Consists of the dimension of the vector (how many elements) and a SparseData
 * structure that stores the data in a compressed format.
 */
typedef struct {
	int4 vl_len_;   /**< This is unused at the moment */
	int4 dimension; /**< Number of elements in this vector, special case is -1 indicates a scalar */
	char data[1];   /**< The serialized SparseData representing the vector here */
} SvecType;

#define DatumGetSvecTypeP(X)           ((SvecType *) PG_DETOAST_DATUM(X))
#define DatumGetSvecTypePCopy(X)       ((SvecType *) PG_DETOAST_DATUM_COPY(X))
#define PG_GETARG_SVECTYPE_P(n)        DatumGetSvecTypeP(PG_GETARG_DATUM(n))
#define PG_GETARG_SVECTYPE_P_COPY(n)   DatumGetSvecTypePCopy(PG_GETARG_DATUM(n))
#define PG_RETURN_SVECTYPE_P(x)        PG_RETURN_POINTER(x)

/* Below are the locations of the SparseData values within the serialized
 * inline SparseData below the Svec header
 *
 * All macros take an (SvecType *) as argument
 */
#define SVECHDRSIZE	(VARHDRSZ + sizeof(int4))
/* Beginning of the serialized SparseData */
#define SVEC_SDATAPTR(x)	((char *)(x)+SVECHDRSIZE)
#define SVEC_SIZEOFSERIAL(x)	(SVECHDRSIZE+SIZEOF_SPARSEDATASERIAL((SparseData)SVEC_SDATAPTR(x)))
#define SVEC_UNIQUE_VALCNT(x)	(SDATA_UNIQUE_VALCNT(SVEC_SDATAPTR(x)))
#define SVEC_TOTAL_VALCNT(x)	(SDATA_TOTAL_VALCNT(SVEC_SDATAPTR(x)))
#define SVEC_DATA_SIZE(x) 	(SDATA_DATA_SIZE(SVEC_SDATAPTR(x)))
#define SVEC_VALS_PTR(x)	(SDATA_VALS_PTR(SVEC_SDATAPTR(x)))
/* The size of the index is variable unlike the values, so in the serialized 
 * SparseData, we include an int32 that indicates the size of the index.
 */
#define SVEC_INDEX_SIZE(x) 	(SDATA_INDEX_SIZE(SVEC_SDATAPTR(x)))
#define SVEC_INDEX_PTR(x) 	(SDATA_INDEX_PTR(SVEC_SDATAPTR(x)))

/** @return True if input is a scalar */
#define IS_SCALAR(x)	(((x)->dimension) < 0 ? 1 : 0 )

/** @return True if input is a NULL, represented internally as a NVP */
#define IS_NVP(x)  (memcmp(&(x),&(NVP),sizeof(double)) == 0)

static inline int check_scalar(int i1, int i2)
{
	if ((!i1) && (!i2)) return(0);
	else if (i1 && i2) return(3);
	else if (i1)  return(1);
	else if (i2) return(2);
	return(0);
}

/*
 * This routine supplies a pointer to a SparseData derived from an SvecType.
 * The SvecType is a serialized structure with fixed memory allocations, so
 * care must be taken not to append to the embedded StringInfo structs
 * without re-serializing the SparseData into the SvecType.
 */
static inline SparseData sdata_from_svec(SvecType *svec)
{
	char *sdataptr   = SVEC_SDATAPTR(svec);
	SparseData sdata = (SparseData)sdataptr;
	sdata->vals  = (StringInfo)SDATA_DATA_SINFO(sdataptr);
	sdata->index = (StringInfo)SDATA_INDEX_SINFO(sdataptr);
	sdata->vals->data   = SVEC_VALS_PTR(svec);
	if (sdata->index->maxlen == 0)
	{
		sdata->index->data = NULL;
	} else
	{
		sdata->index->data  = SVEC_INDEX_PTR(svec);
	}
	return(sdata);
}

static inline void printout_svec(SvecType *svec, char *msg, int stop);
static inline void printout_svec(SvecType *svec, char *msg, int stop)
{
	printout_sdata((SparseData)SVEC_SDATAPTR(svec), msg, stop);
	elog(NOTICE,"len,dimension=%d,%d",VARSIZE(svec),svec->dimension);
}

char *svec_out_internal(SvecType *svec);
SvecType *svec_from_sparsedata(SparseData sdata,bool trim);
ArrayType *svec_return_array_internal(SvecType *svec);
char *svec_out_internal(SvecType *svec);
SvecType *svec_make_scalar(float8 value);
SvecType *svec_from_float8arr(float8 *array, int dimension);
SvecType *op_svec_by_svec_internal(enum operation_t operation, SvecType *svec1, SvecType *svec2);
SvecType *svec_operate_on_sdata_pair(int scalar_args,enum operation_t operation,SparseData left,SparseData right);
SvecType *makeEmptySvec(int allocation);
SvecType *reallocSvec(SvecType *source);

Datum svec_in(PG_FUNCTION_ARGS);
Datum svec_out(PG_FUNCTION_ARGS);
Datum svec_return_vector(PG_FUNCTION_ARGS);
Datum svec_return_array(PG_FUNCTION_ARGS);
Datum svec_send(PG_FUNCTION_ARGS);
Datum svec_recv(PG_FUNCTION_ARGS);

// Operators
Datum svec_pow(PG_FUNCTION_ARGS);
Datum svec_equals(PG_FUNCTION_ARGS);
Datum svec_minus(PG_FUNCTION_ARGS);
Datum svec_plus(PG_FUNCTION_ARGS);
Datum svec_div(PG_FUNCTION_ARGS);
Datum svec_dot(PG_FUNCTION_ARGS);
Datum svec_l2norm(PG_FUNCTION_ARGS);
Datum svec_count(PG_FUNCTION_ARGS);
Datum svec_mult(PG_FUNCTION_ARGS);
Datum svec_log(PG_FUNCTION_ARGS);
Datum svec_l1norm(PG_FUNCTION_ARGS);
Datum svec_summate(PG_FUNCTION_ARGS);

Datum float8arr_minus_float8arr(PG_FUNCTION_ARGS);
Datum svec_minus_float8arr(PG_FUNCTION_ARGS);
Datum float8arr_minus_svec(PG_FUNCTION_ARGS);
Datum float8arr_plus_float8arr(PG_FUNCTION_ARGS);
Datum svec_plus_float8arr(PG_FUNCTION_ARGS);
Datum float8arr_plus_svec(PG_FUNCTION_ARGS);
Datum float8arr_mult_float8arr(PG_FUNCTION_ARGS);
Datum svec_mult_float8arr(PG_FUNCTION_ARGS);
Datum float8arr_mult_svec(PG_FUNCTION_ARGS);
Datum float8arr_div_float8arr(PG_FUNCTION_ARGS);
Datum svec_div_float8arr(PG_FUNCTION_ARGS);
Datum float8arr_div_svec(PG_FUNCTION_ARGS);
Datum svec_dot_float8arr(PG_FUNCTION_ARGS);
Datum float8arr_dot_svec(PG_FUNCTION_ARGS);

// Casts
Datum svec_cast_int2(PG_FUNCTION_ARGS);
Datum svec_cast_int4(PG_FUNCTION_ARGS);
Datum svec_cast_int8(PG_FUNCTION_ARGS);
Datum svec_cast_float4(PG_FUNCTION_ARGS);
Datum svec_cast_float8(PG_FUNCTION_ARGS);
Datum svec_cast_numeric(PG_FUNCTION_ARGS);

Datum float8arr_cast_int2(PG_FUNCTION_ARGS);
Datum float8arr_cast_int4(PG_FUNCTION_ARGS);
Datum float8arr_cast_int8(PG_FUNCTION_ARGS);
Datum float8arr_cast_float4(PG_FUNCTION_ARGS);
Datum float8arr_cast_float8(PG_FUNCTION_ARGS);
Datum float8arr_cast_numeric(PG_FUNCTION_ARGS);

Datum svec_cast_float8arr(PG_FUNCTION_ARGS);
Datum svec_unnest(PG_FUNCTION_ARGS);
Datum svec_pivot(PG_FUNCTION_ARGS);

#endif  /* SPARSEVECTOR_H */
