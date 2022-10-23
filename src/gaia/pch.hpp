#include <cstdlib>
#include <climits>

#include <memory>
#include <numeric>
#include <algorithm>
#include <vector>
#include <unordered_map> // TODO: Write/use a real hashmap!

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

#include <imgui.h>

#include "GaiaDefs.hpp"
#include "Math/GaiaMath.hpp"
#include "GaiaGfxTypes.hpp"
#include "GaiaUtils.hpp"
#include "Span.hpp"
