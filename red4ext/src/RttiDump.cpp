// Reflection dumps: what the engine's RTTI says about native types, and the
// RTTI-visible state of live objects.

#include "Plugin.hpp"

#include <RED4ext/Detail/AddressHashes.hpp>
#include <RED4ext/Relocation.hpp>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace BCAI
{
namespace
{
std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(), [](unsigned char c) { return std::tolower(c); });
    return aText;
}

std::vector<std::string> SplitFilters(const std::string& aFilters)
{
    std::vector<std::string> out;
    std::stringstream ss(aFilters);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty())
            out.push_back(Lower(item));
    }
    return out;
}

bool Matches(const char* aName, const std::vector<std::string>& aFilters)
{
    auto name = Lower(aName ? aName : "");
    for (const auto& f : aFilters)
        if (name.find(f) != std::string::npos)
            return true;
    return aFilters.empty();
}

const char* KindName(RED4ext::rtti::ERTTIType aKind)
{
    using K = RED4ext::rtti::ERTTIType;
    switch (aKind)
    {
    case K::Name: return "name";
    case K::Fundamental: return "fundamental";
    case K::Class: return "class";
    case K::Array: return "array";
    case K::Simple: return "simple";
    case K::Enum: return "enum";
    case K::StaticArray: return "static array";
    case K::NativeArray: return "native array";
    case K::Pointer: return "pointer";
    case K::Handle: return "handle";
    case K::WeakHandle: return "weak handle";
    case K::ResourceReference: return "resource";
    case K::ResourceAsyncReference: return "async resource";
    case K::BitField: return "bitfield";
    case K::LegacySingleChannelCurve: return "curve";
    case K::ScriptReference: return "script ref";
    case K::FixedArray: return "fixed array";
    }
    return "?";
}

// A class's own properties: the scripted list plus the native-only list
// (unk118 also holds properties that never appear in `props`).
std::vector<RED4ext::CProperty*> OwnProperties(RED4ext::CClass* aClass)
{
    std::vector<RED4ext::CProperty*> out;
    std::set<RED4ext::CProperty*> seen;
    for (auto* p : aClass->props)
        if (p && seen.insert(p).second)
            out.push_back(p);
    for (auto* p : aClass->unk118)
        if (p && p->parent == aClass && seen.insert(p).second)
            out.push_back(p);
    std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->valueOffset < b->valueOffset; });
    return out;
}

void DumpClass(std::ostream& aOut, RED4ext::CClass* aClass)
{
    aOut << "class " << aClass->GetName().ToString();
    for (auto* p = aClass->parent; p; p = p->parent)
        aOut << " : " << p->GetName().ToString();
    aOut << Format("  [size 0x%X%s%s%s]\n", aClass->size, aClass->flags.isNative ? ", native" : "",
                   aClass->flags.isScriptedClass ? ", scripted" : "", aClass->flags.isAbstract ? ", abstract" : "");
    for (auto* prop : OwnProperties(aClass))
    {
        aOut << Format("  +0x%04X  %-40s %s%s\n", prop->valueOffset, prop->name.ToString(),
                       prop->type ? prop->type->GetName().ToString() : "?",
                       prop->flags.inValueHolder ? "  (script value holder)" : "");
    }
    for (auto* func : aClass->funcs)
        if (func)
            aOut << "  func " << func->fullName.ToString() << (func->flags.isNative ? "  (native)" : "") << "\n";
    for (auto* func : aClass->staticFuncs)
        if (func)
            aOut << "  static func " << func->fullName.ToString() << (func->flags.isNative ? "  (native)" : "")
                 << "\n";
    aOut << "\n";
}

// ---- live values ----

void Indent(std::ostream& aOut, int aLevel)
{
    for (int i = 0; i < aLevel; ++i)
        aOut << "  ";
}

void DumpValue(std::ostream& aOut, RED4ext::rtti::IType* aType, void* aValue, int aDepth, int aLevel,
               std::set<void*>& aSeen);

void DumpFields(std::ostream& aOut, RED4ext::CClass* aClass, void* aInstance, int aDepth, int aLevel,
                std::set<void*>& aSeen)
{
    std::vector<RED4ext::CClass*> chain;
    for (auto* c = aClass; c; c = c->parent)
        chain.push_back(c);
    std::reverse(chain.begin(), chain.end());
    for (auto* cls : chain)
    {
        for (auto* prop : OwnProperties(cls))
        {
            if (!prop->type)
                continue;
            Indent(aOut, aLevel);
            aOut << prop->name.ToString() << " (" << prop->type->GetName().ToString() << ") = ";
            DumpValue(aOut, prop->type, prop->GetValuePtr<void>(aInstance), aDepth, aLevel + 1, aSeen);
        }
    }
}

void DumpObjectRef(std::ostream& aOut, RED4ext::ISerializable* aObject, int aDepth, int aLevel,
                   std::set<void*>& aSeen)
{
    if (!aObject)
    {
        aOut << "null\n";
        return;
    }
    auto* cls = aObject->GetType();
    aOut << (cls ? cls->GetName().ToString() : "?") << Format(" @%p", aObject);
    if (aDepth <= 0 || !cls || !aSeen.insert(aObject).second)
    {
        aOut << "\n";
        return;
    }
    aOut << "\n";
    DumpFields(aOut, cls, aObject, aDepth - 1, aLevel, aSeen);
}

void DumpValue(std::ostream& aOut, RED4ext::rtti::IType* aType, void* aValue, int aDepth, int aLevel,
               std::set<void*>& aSeen)
{
    using K = RED4ext::rtti::ERTTIType;
    if (!aValue)
    {
        aOut << "<no value>\n";
        return;
    }
    switch (aType->GetType())
    {
    case K::Name:
    case K::Fundamental:
    case K::Simple:
    case K::Enum:
    case K::BitField:
    {
        RED4ext::CString text;
        aType->ToString(aValue, text);
        aOut << text.c_str() << "\n";
        return;
    }
    case K::Class:
    {
        auto* cls = static_cast<RED4ext::CClass*>(aType);
        aOut << "{\n";
        // Structs are values: they don't use up depth unless they're big.
        DumpFields(aOut, cls, aValue, cls->size > 0x100 ? aDepth - 1 : aDepth, aLevel, aSeen);
        Indent(aOut, aLevel - 1);
        aOut << "}\n";
        return;
    }
    case K::Handle:
    {
        auto* handle = static_cast<RED4ext::Handle<RED4ext::ISerializable>*>(aValue);
        DumpObjectRef(aOut, handle->instance, aDepth, aLevel, aSeen);
        return;
    }
    case K::WeakHandle:
    {
        auto* handle = static_cast<RED4ext::WeakHandle<RED4ext::ISerializable>*>(aValue);
        bool alive = handle->instance && handle->refCount && handle->refCount->strongRefs > 0;
        DumpObjectRef(aOut, alive ? handle->instance : nullptr, aDepth, aLevel, aSeen);
        return;
    }
    case K::Array:
    case K::StaticArray:
    case K::NativeArray:
    case K::FixedArray:
    {
        auto* arr = static_cast<RED4ext::CRTTIBaseArrayType*>(aType);
        uint32_t length = arr->GetLength(aValue);
        aOut << "[" << length << "]\n";
        uint32_t shown = std::min<uint32_t>(length, 16);
        for (uint32_t i = 0; i < shown; ++i)
        {
            Indent(aOut, aLevel);
            aOut << "[" << i << "] = ";
            DumpValue(aOut, arr->GetInnerType(), arr->GetElement(aValue, i), aDepth - 1, aLevel + 1, aSeen);
        }
        if (shown < length)
        {
            Indent(aOut, aLevel);
            aOut << "... " << (length - shown) << " more\n";
        }
        return;
    }
    case K::Pointer:
        aOut << Format("ptr %p\n", *static_cast<void**>(aValue));
        return;
    default:
        aOut << "<" << KindName(aType->GetType()) << ">\n";
        return;
    }
}
} // namespace

int DumpTypes(const std::string& aFilters, std::filesystem::path& aOutPath)
{
    auto filters = SplitFilters(aFilters);
    auto* rtti = RED4ext::CRTTISystem::Get();

    RED4ext::DynArray<RED4ext::CClass*> classes;
    rtti->GetClasses(nullptr, classes, nullptr, true);
    std::vector<RED4ext::CClass*> picked;
    for (auto* cls : classes)
        if (cls && Matches(cls->GetName().ToString(), filters))
            picked.push_back(cls);
    std::sort(picked.begin(), picked.end(), [](auto* a, auto* b)
              { return std::string(a->GetName().ToString()) < std::string(b->GetName().ToString()); });

    RED4ext::DynArray<RED4ext::CEnum*> enums;
    rtti->GetEnums(enums);

    std::string tag = filters.empty() ? "all" : aFilters;
    std::replace_if(tag.begin(), tag.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '_');
    aOutPath = DumpDir() / ("types_" + tag.substr(0, 60) + ".txt");
    std::ofstream out(aOutPath);
    out << "# Better Car AI type dump, filters: " << aFilters << "\n";
    out << "# Offsets are from the object start (or from the script value holder where marked).\n\n";
    for (auto* cls : picked)
        DumpClass(out, cls);

    int enumCount = 0;
    for (auto* e : enums)
    {
        if (!e || !Matches(e->GetName().ToString(), filters))
            continue;
        ++enumCount;
        out << "enum " << e->GetName().ToString() << "\n";
        for (uint32_t i = 0; i < e->hashList.size() && i < e->valueList.size(); ++i)
            out << "  " << e->hashList[i].ToString() << " = " << e->valueList[i] << "\n";
        out << "\n";
    }
    Log("type dump '%s': %d classes, %d enums -> %ls", aFilters.c_str(), static_cast<int>(picked.size()), enumCount,
        aOutPath.c_str());
    return static_cast<int>(picked.size()) + enumCount;
}

int DumpNativeFunctions(const std::string& aFilters, std::filesystem::path& aOutPath)
{
    auto filters = SplitFilters(aFilters);
    auto* rtti = RED4ext::CRTTISystem::Get();
    uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // The game's table of native handlers. NativeMemberFunction's constructor
    // (exe+0x6BDCE4 in 2.5) stores the C++ function in this array at index
    // GetRegIndex(); the relocated address is the array itself, not a pointer
    // to it (the SDK's GetHandler dereferences it once too often).
    static RED4ext::UniversalRelocPtr<uintptr_t> handlers(
        RED4ext::Detail::AddressHashes::CBaseFunction_Handlers);

    auto rva = [exe](uintptr_t aAddr) -> std::string
    {
        if (!aAddr)
            return "-";
        if (aAddr < exe || aAddr >= exe + 0x10000000)
            return Format("abs %p", reinterpret_cast<void*>(aAddr));
        return Format("exe+0x%llX", static_cast<unsigned long long>(aAddr - exe));
    };
    auto describe = [&](RED4ext::CBaseFunction* aFunc) -> std::string
    {
        uint32_t index = aFunc->GetRegIndex();
        uintptr_t handler = 0;
        uintptr_t* table = handlers.GetAddr();
        if (table)
            SafeRead(table + index, &handler, sizeof(handler));
        // Member natives run through an "invokable" object: its vtable slot 2.
        uintptr_t invokable = reinterpret_cast<uintptr_t>(aFunc->GetInvokable());
        uintptr_t vtable = 0, execute = 0;
        if (invokable && SafeRead(reinterpret_cast<void*>(invokable), &vtable, sizeof(vtable)))
            SafeRead(reinterpret_cast<void*>(vtable + 2 * sizeof(void*)), &execute, sizeof(execute));
        return Format("index %u, handler %s, invokable execute %s", index, rva(handler).c_str(),
                      rva(execute).c_str());
    };

    std::string tag = filters.empty() ? "all" : aFilters;
    std::replace_if(tag.begin(), tag.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '_');
    aOutPath = DumpDir() / ("natives_" + tag.substr(0, 60) + ".txt");
    std::ofstream out(aOutPath);
    out << "# Better Car AI native function addresses, filters: " << aFilters << "\n";
    out << Format("# game exe loaded at %p (addresses below are relative to it)\n\n", reinterpret_cast<void*>(exe));

    int count = 0;
    RED4ext::DynArray<RED4ext::CClass*> classes;
    rtti->GetClasses(nullptr, classes, nullptr, true);
    for (auto* cls : classes)
    {
        if (!cls)
            continue;
        bool classMatches = Matches(cls->GetName().ToString(), filters);
        auto dumpList = [&](auto& aList, const char* aKind)
        {
            for (auto* func : aList)
            {
                if (!func || !func->flags.isNative)
                    continue;
                if (!classMatches && !Matches(func->fullName.ToString(), filters))
                    continue;
                out << cls->GetName().ToString() << "::" << func->fullName.ToString() << " (" << aKind << "): "
                    << describe(func) << "\n";
                ++count;
            }
        };
        dumpList(cls->funcs, "member");
        dumpList(cls->staticFuncs, "static");
    }
    RED4ext::DynArray<RED4ext::CBaseFunction*> globals;
    rtti->GetGlobalFunctions(globals);
    for (auto* func : globals)
    {
        if (!func || !func->flags.isNative || !Matches(func->fullName.ToString(), filters))
            continue;
        out << "global " << func->fullName.ToString() << ": " << describe(func) << "\n";
        ++count;
    }
    Log("native function dump '%s': %d functions -> %ls", aFilters.c_str(), count, aOutPath.c_str());
    return count;
}

std::string DumpObject(RED4ext::IScriptable* aObject, int aDepth)
{
    std::ostringstream out;
    std::set<void*> seen;
    DumpObjectRef(out, aObject, std::max(aDepth, 1), 1, seen);
    return out.str();
}
} // namespace BCAI
