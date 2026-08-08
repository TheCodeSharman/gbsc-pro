#ifndef GEOMETRY_H_
#define GEOMETRY_H_

// The scaler's output geometry: capture window in, blanking registers out.
//
//     magnification = 1024 / VDS_?SCALE            (BYPS means 1:1)
//     produced      = capture x magnification      simple multiply, both axes
//     write start   = VDS_?B_SP + startConst + startPerMag x magnification

#include "Scale.h"
#include "CaptureWindow.h"
#include "RasterFit.h"
#include "PictureOrigin.h"
#include "AxisSolution.h"
#include "Axis.h"  // Tv5725::Axis, AxisHorizontal, AxisVertical
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "ControlSteps.h"

#endif  // GEOMETRY_H_
