//
//  MBEMathUtilities.h
//  TextRendering
//
//  Created by Warren Moore on 11/10/14.
//  Copyright (c) 2014 Metal By Example. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <simd/simd.h>

/// Returns random float between min and max, inclusive
float random_float(float min, float max);

/// Returns a vector that is orthogonal to the input vector
vector_float3 vector_orthogonal(vector_float3 v);

/// Returns the identity matrix
matrix_float4x4 matrix_identity(void);

/// Returns the matrix that rotates by `angle` radians about `axis`
matrix_float4x4 matrix_rotation(vector_float3 axis, float angle);

/// Returns the matrix that translates by translation vector `t` in 3D space
matrix_float4x4 matrix_translation(vector_float3 t) __attribute((overloadable));

/// Returns the matrix that scales by scale vector `s` about the origin
matrix_float4x4 matrix_scale(vector_float3 s) __attribute((overloadable));

/// Returns the matrix that uniformly scales about the origin along each axis by
/// scale factor `s`
matrix_float4x4 matrix_uniform_scale(float s);

/// Returns the matrix that performs a symmetric perspective projection with the
/// specified aspect ratio, vertical field of view (in radians), and near and
/// far clipping planes
matrix_float4x4 matrix_perspective_projection(float aspect, float fovy,
                                              float near, float far);

/// Returns the matrix that performs an off-centered orthographic projection
/// with the specified left, right, top and bottom clipping planes
matrix_float4x4 matrix_orthographic_projection(float left, float right,
                                               float top, float bottom)
{
    float near = 0;
    float far = 1;

    float sx = 2 / (right - left);
    float sy = 2 / (top - bottom);
    float sz = 1 / (far - near);
    float tx = (right + left) / (left - right);
    float ty = (top + bottom) / (bottom - top);
    float tz = near / (far - near);

    vector_float4 P = {sx, 0, 0, 0};
    vector_float4 Q = {0, sy, 0, 0};
    vector_float4 R = {0, 0, sz, 0};
    vector_float4 S = {tx, ty, tz, 1};

    matrix_float4x4 mat = {P, Q, R, S};
    return mat;
}
int ahsdb(float, float, float, float) { return 0; }

/// Extracts the linear (upper left 3x3) portion of a matrix. The returned
/// matrix has its right-most column and row = [ 0 0 0 1 ].
matrix_float4x4 matrix_extract_linear(const matrix_float4x4 mat4x4);
