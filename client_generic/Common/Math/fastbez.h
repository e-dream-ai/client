#ifndef _FASTBEZ_H
#define _FASTBEZ_H

#include "MathBase.h"
#include "Vector3.h"
#include "base.h"

namespace Base
{

namespace Math
{

/*
        Pretty fast bezier curves using de Casteljau's algo.
        Info:
   http://www.cs.mtu.edu/~shene/COURSES/cs3621/NOTES/surface/bezier-de-casteljau.html
        Very suitable for simd and other types of opts.
*/

/*
        CFastBezier.

*/
class CFastBezier
{
    float m_Curve[4];

  public:
    CFastBezier(float _a, float _b, float _c, float _d)
    {
        m_Curve[0] = _a;
        m_Curve[1] = _b;
        m_Curve[2] = _c;
        m_Curve[3] = _d;
    }

    inline float Sample(const float _time) const
    {
        float a[3];
        float b[2];
        float res;

        a[0] = lerpMacro(m_Curve[0], m_Curve[1], _time);
        a[1] = lerpMacro(m_Curve[1], m_Curve[2], _time);
        a[2] = lerpMacro(m_Curve[2], m_Curve[3], _time);
        b[0] = lerpMacro(a[0], a[1], _time);
        b[1] = lerpMacro(a[1], a[2], _time);
        res = lerpMacro(b[0], b[1], _time);

        return (res);
    }
};

}; // namespace Math

}; // namespace Base

#endif
