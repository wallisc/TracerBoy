#pragma once

#include "targetver.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <assert.h>
#include <exception>

#include "SceneParser.h"
#include <string>
#include <iostream>
#include <fstream>

#include "PlyParser.h"
#include "PbrtParser.h"