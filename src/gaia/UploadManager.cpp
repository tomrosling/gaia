#include "UploadManager.hpp"
#include "CommandQueue.hpp"

namespace gaia
{

void UploadManager::AddIntermediateResource(ID3D12Resource* resource)
{
    m_intermediateResources.push_back(resource);
}

void UploadManager::BeginFrame(CommandQueue& commandQueue)
{
    // Wait for last frame's uploads to complete.
    if (m_fenceValue != 0)
    {
        commandQueue.WaitFence(m_fenceValue);
        m_fenceValue = 0;
    }

    // Destroy any resources references we were holding while they uploaded.
    m_intermediateResources.clear();
}

}
