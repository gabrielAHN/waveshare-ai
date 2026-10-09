#pragma once

#include <math.h>
#include "reasings.h"

/* Selected finite scalar wrappers. Calls are bounded before the imported equations so a zero/invalid
 * duration cannot divide, endpoints are exact, and monotonic curves cannot escape start..end. */
static inline float ui_ease_guard(float t,float b,float c,float d,int cubic){
 if(!isfinite(b)||!isfinite(c))return 0.f;
 float end=b+c;
 if(!(d>0.f)||!isfinite(d))return end;
 if(isnan(t)||t<=0.f)return b;
 if(!isfinite(t)||t>=d)return end;
 float v=cubic?EaseCubicOut(t,b,c,d):EaseQuadInOut(t,b,c,d);
 if(!isfinite(v))return end;
 float lo=b<end?b:end,hi=b>end?b:end;
 return v<lo?lo:(v>hi?hi:v);
}
static inline float ui_ease_quad_in_out(float t,float b,float c,float d){return ui_ease_guard(t,b,c,d,0);}
static inline float ui_ease_cubic_out(float t,float b,float c,float d){return ui_ease_guard(t,b,c,d,1);}
