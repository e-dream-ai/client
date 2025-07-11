/*
        4 component vector class.
        NOTE. Will be typedefed and included from vector4.h.
*/
#ifndef _VECTOR4_X86_H
#define _VECTOR4_X86_H

#include "Vector3_X86.h"

namespace Base
{

namespace Math
{

/*
        CVector4_x86().

*/
class CVector4_x86
{
  public:
    CVector4_x86();
    CVector4_x86(const float _x, const float _y, const float _z,
                 const float _w = 0.0f);
    CVector4_x86(const CVector4_x86& _vec);
    CVector4_x86(const CVector3_x86& _vec3); //	.m_W = 1.0f.

    //
    void Set(const float _x, const float _y, const float _z, const float _w);
    void Set(const CVector4_x86& _v);
    void Set(const CVector3_x86& _v); //	.m_W = 1.0f.

    //
    float Len() const;
    float LenSqr() const;
    float RLen() const;
    float RLenFast() const;

    //
    void Normalize();
    void NormalizeFast();
    void OneOver();

    CVector4_x86 GetOneOver();

    //
    void operator+=(const CVector4_x86& _v);
    void operator-=(const CVector4_x86& _v);
    void operator*=(const float _s);

    //
    bool operator>(const CVector4_x86& _rhs);
    bool operator<(const CVector4_x86& _rhs);

    //
    CVector4_x86& operator=(const CVector3_x86& _v); //	.m_W = 1.0f.

    //
    float& operator()(const uint32_t _i);
    float operator()(const uint32_t _i) const;

    //
    bool IsEqual(const CVector4_x86& _v, const float _tol) const;
    int32_t Compare(const CVector4_x86& _v,
                    const float _tol) const; //	-1, 0, +1.

    //
    void Minimum(const CVector4_x86& _v);
    void Maximum(const CVector4_x86& _v);

    //
    void Lerp(const CVector4_x86& _v0, const float _delta);
    void Saturate();

    //
    float Dot(const CVector4_x86& _v0) const;

    //
    float m_X;
    float m_Y;
    float m_Z;
    float m_W;
};

/*
 */
inline CVector4_x86::CVector4_x86() : m_X(0.0f), m_Y(0.0f), m_Z(0.0f), m_W(0.0f)
{
}

/*
 */
inline CVector4_x86::CVector4_x86(const float _x, const float _y,
                                  const float _z, const float _w)
    : m_X(_x), m_Y(_y), m_Z(_z), m_W(_w)
{
}

/*
 */
inline CVector4_x86::CVector4_x86(const CVector4_x86& _v)
    : m_X(_v.m_X), m_Y(_v.m_Y), m_Z(_v.m_Z), m_W(_v.m_W)
{
}

/*
 */
inline CVector4_x86::CVector4_x86(const CVector3_x86& _v)
    : m_X(_v.m_X), m_Y(_v.m_Y), m_Z(_v.m_Z), m_W(0.0f)
{
}

/*
 */
inline void CVector4_x86::Set(const float _x, const float _y, const float _z,
                              const float _w)
{
    m_X = _x;
    m_Y = _y;
    m_Z = _z;
    m_W = _w;
}

/*
 */
inline void CVector4_x86::Set(const CVector4_x86& _v)
{
    m_X = _v.m_X;
    m_Y = _v.m_Y;
    m_Z = _v.m_Z;
    m_W = _v.m_W;
}

/*
 */
inline void CVector4_x86::Set(const CVector3_x86& _v)
{
    m_X = _v.m_X;
    m_Y = _v.m_Y;
    m_Z = _v.m_Z;
    m_W = 0.0f;
}

/*
 */
inline float CVector4_x86::Len(void) const
{
    return (Sqrt(m_X * m_X + m_Y * m_Y + m_Z * m_Z + m_W * m_W));
}

/*
 */
inline float CVector4_x86::LenSqr(void) const
{
    return (m_X * m_X + m_Y * m_Y + m_Z * m_Z + m_W * m_W);
}

/*
 */
inline float CVector4_x86::RLen(void) const
{
    return (RSqrt(m_X * m_X + m_Y * m_Y + m_Z * m_Z + m_W * m_W));
}

/*
 */
inline float CVector4_x86::RLenFast(void) const
{
    return (RSqrtFast(m_X * m_X + m_Y * m_Y + m_Z * m_Z + m_W * m_W));
}

/*
        Normalize().
        POTENTIAL BUG.
*/
inline void CVector4_x86::Normalize(void)
{
    float l = RLen();
    m_X *= l;
    m_Y *= l;
    m_Z *= l;
    m_W *= l;
}

/*
        NormalizeFase().
        POTENTIAL BUG.
*/
inline void CVector4_x86::NormalizeFast(void)
{
    float l = RLenFast();
    m_X *= l;
    m_Y *= l;
    m_Z *= l;
    m_W *= l;
}

inline CVector4_x86 CVector4_x86::GetOneOver()
{
    return CVector4_x86(1.0f / m_X, 1.0f / m_Y, 1.0f / m_Z);
}

inline void CVector4_x86::OneOver()
{
    m_X = 1.0f / m_X;
    m_Y = 1.0f / m_Y;
    m_Z = 1.0f / m_Z;
}

/*
 */
inline void CVector4_x86::operator+=(const CVector4_x86& _v)
{
    m_X += _v.m_X;
    m_Y += _v.m_Y;
    m_Z += _v.m_Z;
    m_W += _v.m_W;
}

/*
 */
inline void CVector4_x86::operator-=(const CVector4_x86& _v)
{
    m_X -= _v.m_X;
    m_Y -= _v.m_Y;
    m_Z -= _v.m_Z;
    m_W -= _v.m_W;
}

/*
 */
inline void CVector4_x86::operator*=(const float _s)
{
    m_X *= _s;
    m_Y *= _s;
    m_Z *= _s;
    m_W *= _s;
}

/*
 */
inline CVector4_x86& CVector4_x86::operator=(const CVector3_x86& _v)
{
    Set(_v);
    return (*this);
}

/*
 */
inline bool CVector4_x86::IsEqual(const CVector4_x86& _v,
                                  const float _tol) const
{
    if (fabsf(_v.m_X - m_X) > _tol)
        return (false);
    else if (fabsf(_v.m_Y - m_Y) > _tol)
        return (false);
    else if (fabsf(_v.m_Z - m_Z) > _tol)
        return (false);
    else if (fabsf(_v.m_W - m_W) > _tol)
        return (false);
    return true;
}

/*
 */
inline int32_t CVector4_x86::Compare(const CVector4_x86& _v,
                                     const float _tol) const
{
    if (fabsf(_v.m_X - m_X) > _tol)
        return ((_v.m_X > m_X) ? +1 : -1);
    else if (fabsf(_v.m_Y - m_Y) > _tol)
        return ((_v.m_Y > m_Y) ? +1 : -1);
    else if (fabsf(_v.m_Z - m_Z) > _tol)
        return ((_v.m_Z > m_Z) ? +1 : -1);
    else if (fabsf(_v.m_W - m_W) > _tol)
        return ((_v.m_W > m_W) ? +1 : -1);
    else
        return (0);
}

/*
 */
inline bool CVector4_x86::operator>(const CVector4_x86& _rhs)
{
    if ((m_X > _rhs.m_X) || (m_Y > _rhs.m_Y) || (m_Z > _rhs.m_Z) ||
        (m_W > _rhs.m_W))
        return (true);
    else
        return (false);
}

/*
 */
inline bool CVector4_x86::operator<(const CVector4_x86& _rhs)
{
    if ((m_X < _rhs.m_X) || (m_Y < _rhs.m_Y) || (m_Z < _rhs.m_Z) ||
        (m_W < _rhs.m_W))
        return (true);
    else
        return (false);
}

/*
 */
static inline CVector4_x86 operator+(const CVector4_x86& _v0,
                                     const CVector4_x86& _v1)
{
    return (CVector4_x86(_v0.m_X + _v1.m_X, _v0.m_Y + _v1.m_Y,
                         _v0.m_Z + _v1.m_Z, _v0.m_W + _v1.m_W));
}

/*
 */
static inline CVector4_x86 operator-(const CVector4_x86& _v0,
                                     const CVector4_x86& _v1)
{
    return (CVector4_x86(_v0.m_X - _v1.m_X, _v0.m_Y - _v1.m_Y,
                         _v0.m_Z - _v1.m_Z, _v0.m_W - _v1.m_W));
}

/*
 */
static inline CVector4_x86 operator*(const CVector4_x86& _v0, const float& _s)
{
    return (
        CVector4_x86(_v0.m_X * _s, _v0.m_Y * _s, _v0.m_Z * _s, _v0.m_W * _s));
}

/*
        Dot product.
*/
static inline float operator%(const CVector4_x86& _v0, const CVector4_x86& _v1)
{
    return (_v0.m_X * _v1.m_X + _v0.m_Y * _v1.m_Y + _v0.m_Z * _v1.m_Z +
            _v0.m_W * _v1.m_W);
}

/*
        Cross product.

*/
static inline CVector4_x86 operator*(const CVector4_x86& _v0,
                                     const CVector4_x86& _v1)
{
    return (CVector4_x86(_v0.m_Y * _v1.m_Z - _v0.m_Z * _v1.m_Y,
                         _v0.m_Z * _v1.m_X - _v0.m_X * _v1.m_Z,
                         _v0.m_X * _v1.m_Y - _v0.m_Y * _v1.m_X));
}

/*
 */
static inline CVector4_x86 operator-(const CVector4_x86& _v)
{
    return (CVector4_x86(-_v.m_X, -_v.m_Y, -_v.m_Z, -_v.m_W));
}

//
inline float& CVector4_x86::operator()(const uint32_t _i)
{
    return ((float*)this)[_i];
}

//
inline float CVector4_x86::operator()(const uint32_t _i) const
{
    return ((const float*)this)[_i];
}

/*
 */
inline void CVector4_x86::Lerp(const CVector4_x86& _v0, const float _delta)
{
    m_X = _v0.m_X + ((m_X - _v0.m_X) * _delta);
    m_Y = _v0.m_Y + ((m_Y - _v0.m_Y) * _delta);
    m_Z = _v0.m_Z + ((m_Z - _v0.m_Z) * _delta);
    m_W = _v0.m_W + ((m_W - _v0.m_W) * _delta);
}

/*
 */
inline void CVector4_x86::Saturate(void)
{
    m_X = Clamped(m_X, 0.0f, 1.0f);
    m_Y = Clamped(m_Y, 0.0f, 1.0f);
    m_Z = Clamped(m_Z, 0.0f, 1.0f);
    m_W = Clamped(m_W, 0.0f, 1.0f);
}

/*
        Dot.

*/
inline float CVector4_x86::Dot(const CVector4_x86& _v0) const
{
    return (m_X * _v0.m_X + m_Y * _v0.m_Y + m_Z * _v0.m_Z + m_W * _v0.m_W);
}

inline void CVector4_x86::Minimum(const CVector4_x86& _v)
{
    m_X = std::min(m_X, _v.m_X);
    m_Y = std::min(m_Y, _v.m_Y);
    m_Z = std::min(m_Z, _v.m_Z);
    m_W = std::min(m_W, _v.m_W);
}

inline void CVector4_x86::Maximum(const CVector4_x86& _v)
{
    m_X = std::max(m_X, _v.m_X);
    m_Y = std::max(m_Y, _v.m_Y);
    m_Z = std::max(m_Z, _v.m_Z);
    m_W = std::max(m_W, _v.m_W);
}

}; // namespace Math
}; // namespace Base

#endif
