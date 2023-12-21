#ifndef _RECT_H
#define _RECT_H

#include "Vector2.h"
#include "base.h"

namespace Base
{
namespace Math
{

/*
        CRect.

*/
class CRect
{
  public:
    CRect();
    CRect(const CRect& r);
    CRect(float _w, float _h);
    CRect(float _x0, float _y0, float _x1, float _y1);
    CRect(const CVector2& _a, const CVector2& _b);

    //	'IsNull' is not very clear, should be 'IsInvalid' or 'IsSingularity' or
    // something like that.
    bool IsNull() const;

    //
    float Width() const;
    float Height() const;

    //	Force integer.
    int32_t iWidth() const;
    int32_t iHeight() const;

    //
    float Aspect(void) const;
    float Area(void) const;
    uint32_t iArea(void) const;

    //
    bool IsNormalized() const;
    CRect& Normalize();

    //	Boolean things.
    CRect Intersection(const CRect& _r) const;
    CRect Union(const CRect& _r) const;
    bool Surrounds(const CRect& _r) const;
    bool Inside(const CVector2& _p) const;

    //
    float m_X0, m_Y0;
    float m_X1, m_Y1;
};

/*
 */
inline CRect::CRect() : m_X0(0.0f), m_Y0(0.0f), m_X1(0.0f), m_Y1(0.0f) {}

/*
 */
inline CRect::CRect(const CRect& _r)
    : m_X0(_r.m_X0), m_Y0(_r.m_Y0), m_X1(_r.m_X1), m_Y1(_r.m_Y1)
{
}

/*
 */
inline CRect::CRect(float _w, float _h) : m_X0(0), m_Y0(0), m_X1(_w), m_Y1(_h)
{
}

/*
 */
inline CRect::CRect(float _x0, float _y0, float _x1, float _y1)
    : m_X0(_x0), m_Y0(_y0), m_X1(_x1), m_Y1(_y1)
{
}

/*
 */
inline CRect::CRect(const CVector2& _a, const CVector2& _b)
    : m_X0(_a.m_X), m_Y0(_a.m_Y), m_X1(_b.m_X), m_Y1(_b.m_Y)
{
}

/*
 */
inline bool CRect::IsNull() const
{
    return (Width() == 0.0f && Height() == 0.0f);
}

/*
 */
inline float CRect::Width() const { return (m_X1 - m_X0); }

/*
 */
inline float CRect::Height() const { return (m_Y1 - m_Y0); }

/*
 */
inline int32_t CRect::iWidth() const { return ((int32_t)Width()); }

/*
 */
inline int32_t CRect::iHeight() const { return ((int32_t)Height()); }

/*
 */
inline bool CRect::IsNormalized() const
{
    return ((Width() >= 0.0f) && (Height() >= 0.0f));
}

/*
 */
inline bool CRect::Surrounds(const CRect& _r) const
{
    return ((m_X0 < _r.m_X0) && (m_Y0 < _r.m_Y0) && (m_X1 > _r.m_X1) &&
            (m_Y1 > _r.m_Y1));
}

/*
 */
inline bool CRect::Inside(const CVector2& _p) const
{
    if ((_p.m_X < m_X0) || (_p.m_X > m_X1))
        return (false);
    if ((_p.m_Y < m_Y0) || (_p.m_Y > m_Y1))
        return (false);

    return (true);
}

/*
 */
inline float CRect::Aspect(void) const { return (Height() / Width()); }

/*
 */
inline float CRect::Area(void) const { return (Width() * Height()); }

/*
 */
inline uint32_t CRect::iArea(void) const
{
    return uint32_t(iWidth() * iHeight());
}

}; // namespace Math

}; // namespace Base

#endif
