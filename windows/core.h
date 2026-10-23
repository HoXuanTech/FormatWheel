#pragma once

#include <string>
#include <vector>

namespace fw {

struct Action {
    const wchar_t* id;
    const wchar_t* label;
    const wchar_t* detail;
};

enum class Kind { image, pdf, video, audio, document, other };

Kind kind_of(const std::wstring& path);
const wchar_t* kind_label(Kind kind);
std::vector<Action> actions(Kind kind, int page);
std::wstring run(const std::wstring& id, const std::vector<std::wstring>& files);
bool tool_available(const std::wstring& executable);
std::wstring unavailable_reason(const std::wstring& id);

} // namespace fw
