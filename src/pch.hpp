#include <memory>
#include <algorithm>
#include <cstdlib>
#include <climits>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>

#undef max
#undef min

#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include "GaiaDefs.hpp"
#include "GaiaMath.hpp"
