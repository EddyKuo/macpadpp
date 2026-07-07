#include "extension/ExtensionRegistry.h"

#include "extension/IExtension.h"

namespace macpad::extension {

ExtensionRegistry::ExtensionRegistry(IHostServices *host)
    : m_host(host)
{
}

ExtensionRegistry::~ExtensionRegistry()
{
    unloadAll();
}

void ExtensionRegistry::load(std::unique_ptr<IExtension> ext)
{
    if (!ext)
        return;
    ext->onLoad(m_host);
    m_extensions.push_back(std::move(ext));
}

void ExtensionRegistry::unloadAll()
{
    for (auto &ext : m_extensions)
        ext->onUnload();
    m_extensions.clear();
}

QVector<ExtensionCapabilities> ExtensionRegistry::capabilitiesList() const
{
    QVector<ExtensionCapabilities> caps;
    for (const auto &ext : m_extensions)
        caps.push_back(ext->capabilities());
    return caps;
}

}  // namespace macpad::extension
