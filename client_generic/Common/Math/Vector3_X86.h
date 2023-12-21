/*
        3 component vector class.
        NOTE. Will be typedefed and included from vector3.h.
*/
#ifndef _VECTOR3_X86_H
#define _VECTOR3_X86_H

namespace Base
{

namespace Math
{

/*
        CVector3_x86().

*/
class CVector3_x86
{
  public:
    CVector3_x86();
    CVector3_x86(const float _x, const float _y, const float _z);
    CVector3_x86(const CVector3_x86& _vec);

    //
    void Set(const float _x, const float _y, const float _z);
    void Set(const CVector3_x86& _vec);

    //
    float Len() const;
    float RLen() const;
    float RLenFast() const;
    float LenSqr() const;

    //
    void Normalize();
    void NormalizeFast();

    //
    void operator+=(const CVector3_x86& _v0);
    void operator-=(const CVector3_x86& _v0);
    void operator*=(float _s);

    //
    bool operator>(const CVector3_x86& _rhs);
    bool operator<(const CVector3_x86& _rhs);

    //
    float& operator()(const uint32_t _i);
    float operator()(const uint32_t _i) const;

    //
    bool IsEqual(const CVector3_x86& _v, const float _tol) const;
    int32_t Compare(const CVector3_x86& _v,
                    const float _tol) const; //	-1, 0, +1.

    //
    void Rotate(const CVector3_x86& _axis, const float _angle);
    void Lerp(const CVector3_x86& _v0, const float _delta);

    //	Returns a vector orthogonal to self, not normalized.
    CVector3_x86 FindOrtho(void) const;

    //
    void Saturate(void);
    float Dot(const CVector3_x86& _v0) const;
    static float Distance(const CVector3_x86& _v0, const CVector3_x86& _v1);

    //
    float m_X;
    float m_Y;
    float m_Z;
};

/*
 */
inline CVector3_x86::CVector3_x86() : m_X(0.0f), m_Y(0.0f), m_Z(0.0f) {}

/*
 */
inline CVector3_x86::CVector3_x86(const float _x, const float _y,
                                  const float _z)
    : m_X(_x), m_Y(_y), m_Z(_z)
{
}

/*
 */
inline CVector3_x86::CVector3_x86(const CVector3_x86& _vec)
    : m_X(_vec.m_X), m_Y(_vec.m_Y), m_Z(_vec.m_Z)
{
}

/*
 */
inline void CVector3_x86::Set(const float _x, const float _y, const float _z)
{
    m_X = _x;
    m_Y = _y;
    m_Z = _z;
}

/*
 */
inline void CVector3_x86::Set(const CVector3_x86& _vec)
{
    m_X = _vec.m_X;
    m_Y = _vec.m_Y;
    m_Z = _vec.m_Z;
}

/*
 */
inline float CVector3_x86::Len(void) const
{
    return (Sqrt(m_X * m_X + m_Y * m_Y + m_Z * m_Z));
}

/*
 */
inline float CVector3_x86::RLen(void) const
{
    return (RSqrt(m_X * m_X + m_Y * m_Y + m_Z * m_Z));
}

/*
 */
inline float CVector3_x86::RLenFast(void) const
{
    return (RSqrtFast(m_X * m_X + m_Y * m_Y + m_Z * m_Z));
}

/*
 */
inline float CVector3_x86::LenSqr(void) const
{
    return (m_X * m_X + m_Y * m_Y + m_Z * m_Z);
}

/*
        Normalize().
        POTENTIAL BUG.
*/
inline void CVector3_x86::Normalize(void)
{
    float l = RLen();
    m_X *= l;
    m_Y *= l;
    m_Z *= l;
}

/*
        NormalizeFast().
        POTENTIAL BUG.
*/
inline void CVector3_x86::NormalizeFast(void)
{
    float l = RLenFast();
    m_X *= l;
    m_Y *= l;
    m_Z *= l;
}

/*
 */
inline void CVector3_x86::operator+=(const CVector3_x86& _v0)
{
    m_X += _v0.m_X;
    m_Y += _v0.m_Y;
    m_Z += _v0.m_Z;
}

/*
 */
inline void CVector3_x86::operator-=(const CVector3_x86& _v0)
{
    m_X -= _v0.m_X;
    m_Y -= _v0.m_Y;
    m_Z -= _v0.m_Z;
}

/*
 */
inline void CVector3_x86::operator*=(float _s)
{
    m_X *= _s;
    m_Y *= _s;
    m_Z *= _s;
}

/*
 */
inline bool CVector3_x86::IsEqual(const CVector3_x86& _v,
                                  const float _tol) const
{
    if (fabsf(_v.m_X - m_X) > _tol)
        return (false);
    else if (fabsf(_v.m_Y - m_Y) > _tol)
        return (false);
    else if (fabsf(_v.m_Z - m_Z) > _tol)
        return (false);

    return (true);
}

/*
 */
inline int32_t CVector3_x86::Compare(const CVector3_x86& _v,
                                     const float _tol) const
{
    if (fabsf(_v.m_X - m_X) > _tol)
        return ((_v.m_X > m_X) ? +1 : -1);
    else if (fabsf(_v.m_Y - m_Y) > _tol)
        return ((_v.m_Y > m_Y) ? +1 : -1);
    else if (fabsf(_v.m_Z - m_Z) > _tol)
        return ((_v.m_Z > m_Z) ? +1 : -1);
    else
        return (0);
}

/*
 */
inline void CVector3_x86::Rotate(const CVector3_x86& _axis, const float _angle)
{
    float rotM[9];
    float sa, ca;

    SinCos(_angle, &sa, &ca);

    //	Build a rotation matrix.
    rotM[0] = ca + (1 - ca) * _axis.m_X * _axis.m_X;
    rotM[1] = (1 - ca) * _axis.m_X * _axis.m_Y - sa * _axis.m_Z;
    rotM[2] = (1 - ca) * _axis.m_Z * _axis.m_X + sa * _axis.m_Y;
    rotM[3] = (1 - ca) * _axis.m_X * _axis.m_Y + sa * _axis.m_Z;
    rotM[4] = ca + (1 - ca) * _axis.m_Y * _axis.m_Y;
    rotM[5] = (1 - ca) * _axis.m_Y * _axis.m_Z - sa * _axis.m_X;
    rotM[6] = (1 - ca) * _axis.m_Z * _axis.m_X - sa * _axis.m_Y;
    rotM[7] = (1 - ca) * _axis.m_Y * _axis.m_Z + sa * _axis.m_X;
    rotM[8] = ca + (1 - ca) * _axis.m_Z * _axis.m_Z;

    //	Handmade multiplication.
    CVector3_x86 help(rotM[0] * m_X + rotM[1] * m_Y + rotM[2] * m_Z,
                      rotM[3] * m_X + rotM[4] * m_Y + rotM[5] * m_Z,
                      rotM[6] * m_X + rotM[7] * m_Y + rotM[8] * m_Z);

    *this = help;
}

/*
 */
static inline CVector3_x86 operator+(const CVector3_x86& _v0,
                                     const CVector3_x86& _v1)
{
    return (
        CVector3_x86(_v0.m_X + _v1.m_X, _v0.m_Y + _v1.m_Y, _v0.m_Z + _v1.m_Z));
}

/*
 */
static inline CVector3_x86 operator-(const CVector3_x86& _v0,
                                     const CVector3_x86& _v1)
{
    return (
        CVector3_x86(_v0.m_X - _v1.m_X, _v0.m_Y - _v1.m_Y, _v0.m_Z - _v1.m_Z));
}

/*
 */
static inline CVector3_x86 operator*(const CVector3_x86& _v0, const float _s)
{
    return (CVector3_x86(_v0.m_X * _s, _v0.m_Y * _s, _v0.m_Z * _s));
}

/*
 */
static inline CVector3_x86 operator-(const CVector3_x86& _v)
{
    return (CVector3_x86(-_v.m_X, -_v.m_Y, -_v.m_Z));
}

/*
 */
static inline CVector3_x86 operator/(const CVector3_x86& _v0, const float _s)
{
    const float rs = 1.0f / _s;
    return (CVector3_x86(_v0.m_X * rs, _v0.m_Y * rs, _v0.m_Z * rs));
}

/*
        Dot product.
*/
static inline float operator%(const CVector3_x86& _v0, const CVector3_x86& _v1)
{
    return (_v0.m_X * _v1.m_X + _v0.m_Y * _v1.m_Y + _v0.m_Z * _v1.m_Z);
}

/*
        Cross product.
*/
static inline CVector3_x86 operator*(const CVector3_x86& _v0,
                                     const CVector3_x86& _v1)
{
    return (CVector3_x86(_v0.m_Y * _v1.m_Z - _v0.m_Z * _v1.m_Y,
                         _v0.m_Z * _v1.m_X - _v0.m_X * _v1.m_Z,
                         _v0.m_X * _v1.m_Y - _v0.m_Y * _v1.m_X));
}

/*
 */
inline void CVector3_x86::Lerp(const CVector3_x86& _v0, const float _delta)
{
    m_X = _v0.m_X + ((m_X - _v0.m_X) * _delta);
    m_Y = _v0.m_Y + ((m_Y - _v0.m_Y) * _delta);
    m_Z = _v0.m_Z + ((m_Z - _v0.m_Z) * _delta);
}

/*
 */
inline void CVector3_x86::Saturate(void)
{
    m_X = Clamped(m_X, 0.0f, 1.0f);
    m_Y = Clamped(m_Y, 0.0f, 1.0f);
    m_Z = Clamped(m_Z, 0.0f, 1.0f);
}

/*
        Find a vector that is orthogonal to self. Self should not be (0,0,0).
        Return value is not normalized.
*/
inline CVector3_x86 CVector3_x86::FindOrtho(void) const
{
    if (m_X != 0.0f)
    {
        return (CVector3_x86((-m_Y - m_Z) / m_X, 1.0f, 1.0f));
    }
    else if (m_Y != 0.0f)
    {
        return (CVector3_x86(1.0f, (-m_X - m_Z) / m_Y, 1.0f));
    }
    else if (m_Z != 0.0f)
    {
        return (CVector3_x86(1.0f, 1.0f, (-m_X - m_Y) / m_Z));
    }
    else
        return (CVector3_x86(0.0f, 0.0f, 0.0f));
}

/*
        Dot product.
*/
inline float CVector3_x86::Dot(const CVector3_x86& _v0) const
{
    return (m_X * _v0.m_X + m_Y * _v0.m_Y + m_Z * _v0.m_Z);
}

/*
 */
inline bool CVector3_x86::operator>(const CVector3_x86& _rhs)
{
    if ((m_X > _rhs.m_X) || (m_Y > _rhs.m_Y) || (m_Z > _rhs.m_Z))
        return (true);
    else
        return (false);
}

/*
 */
inline bool CVector3_x86::operator<(const CVector3_x86& _rhs)
{
    if ((m_X < _rhs.m_X) || (m_Y < _rhs.m_Y) || (m_Z < _rhs.m_Z))
        return (true);
    else
        return (false);
}

//
inline float& CVector3_x86::operator()(const uint32_t _i)
{
    return ((float*)this)[_i];
}

//
inline float CVector3_x86::operator()(const uint32_t _i) const
{
    return ((const float*)this)[_i];
}

/*
 */
inline float CVector3_x86::Distance(const CVector3_x86& _v0,
                                    const CVector3_x86& _v1)
{
    CVector3_x86 v(_v1 - _v0);
    return (Sqrt(v.m_X * v.m_X + v.m_Y * v.m_Y + v.m_Z * v.m_Z));
}

}; // namespace Math
}; // namespace Base

#endif
