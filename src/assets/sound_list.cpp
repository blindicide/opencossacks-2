#include "assets/sound_list.hpp"

#include <cctype>
#include <cstdlib>

#include "assets/xml_tree.hpp"

namespace oc2::assets {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

float floatOf(const XmlNode& n, const char* field, float fallback) {
    const auto v = n.value(field);
    return v ? std::strtof(v->c_str(), nullptr) : fallback;
}

} // namespace

SoundList SoundList::parse(std::string_view xml) {
    SoundList list;
    const XmlNode doc = parseXml(xml);
    const XmlNode* groups = doc.child("SoundsList");
    if (!groups) return list;
    for (const XmlNode& g : groups->children) {
        SoundGroup group;
        group.name = g.value("GroupName").value_or("");
        if (group.name.empty()) continue;
        group.volume = floatOf(g, "Volume", 1.0f);
        group.minPause = floatOf(g, "MinimalPause(sec)", 0.0f);
        group.pauseVariation = floatOf(g, "PauseVariation(sec)", 0.0f);
        if (const XmlNode* l = g.child("List"))
            for (const XmlNode& s : l->children) {
                SoundVariant v;
                v.file = s.value("WaveFile").value_or("");
                v.volume = floatOf(s, "Volume", 1.0f);
                v.looped = s.boolValue("Looped").value_or(false);
                if (!v.file.empty()) group.variants.push_back(std::move(v));
            }
        list.groups[upper(group.name)] = std::move(group);
    }
    return list;
}

const SoundGroup* SoundList::find(std::string_view name) const {
    auto it = groups.find(upper(name));
    return it == groups.end() ? nullptr : &it->second;
}

} // namespace oc2::assets
