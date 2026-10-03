#pragma once
#include <stdint.h>
namespace CoverLayout {
struct Layout {
 int width,height,x,y,sourceWidth,sourceHeight;
 Layout(int w,int h):sourceWidth(w),sourceHeight(h) {
  if(w*240>=h*320){height=240;width=(w*240+h-1)/h;}
  else{width=320;height=(h*320+w-1)/w;}
  x=(320-width)/2;y=(240-height)/2;
 }
 int left(int sx) const {return x+(sx*width+sourceWidth-1)/sourceWidth;}
 int top(int sy) const {return y+(sy*height+sourceHeight-1)/sourceHeight;}
 int sourceX(int dx) const {return (dx-x)*sourceWidth/width;}
 int sourceY(int dy) const {return (dy-y)*sourceHeight/height;}
};
}
