#ifndef TV5725_DRIVER_H_
#define TV5725_DRIVER_H_

// The scaler's output geometry: capture window in, blanking registers out.
//
//     magnification = 1024 / VDS_?SCALE            (BYPS means 1:1)
//     produced      = capture x magnification      simple multiply, both axes
//     write start   = VDS_?B_SP + startConst + startPerMag x magnification
//
// docs/firmware-geometry-engine.md and docs/scaler-geometry-model.md have the
// measurements.

#include "Scale.h"
#include "CaptureWindow.h"
#include "InputLine.h"
#include "RasterFit.h"
#include "PictureOrigin.h"
#include "AxisSolution.h"
#include "Axis.h"  // Tv5725::Axis, AxisHorizontal, AxisVertical
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "ControlSteps.h"
#include "Memory.h"
#include "Sampling.h"
#include "PresetLoad.h"

#endif  // TV5725_DRIVER_H_
