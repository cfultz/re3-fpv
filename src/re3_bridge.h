#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct CVector {
    float x, y, z;
};

struct RwMatrix {
    CVector right;   uint32_t flags;
    CVector up;      uint32_t pad1;
    CVector at;      uint32_t pad2;
    CVector pos;     uint32_t pad3;
};
static_assert(sizeof(RwMatrix) == 64, "RwMatrix size mismatch");

struct CMatrix {
    union {
        float f[4][4];
        struct {
            float rx, ry, rz, rw; // right
            float fx, fy, fz, fw; // forward (GTA Y-forward)
            float ux, uy, uz, uw; // up (GTA Z-up)
            float px, py, pz, pw; // position
        };
    };
    RwMatrix* m_attachment;
    bool m_hasRwMatrix;
    uint8_t _pad[7];
};
static_assert(sizeof(CMatrix) == 80, "CMatrix size mismatch");

struct CColPoint {
    CVector point;
    int32_t pad1;
    CVector normal;
    int32_t pad2;
    uint8_t surfaceA;
    uint8_t pieceA;
    uint8_t surfaceB;
    uint8_t pieceB;
    float depth;
};
static_assert(sizeof(CColPoint) == 40, "CColPoint size mismatch");

struct CEntity;
struct RwCamera;
struct RwFrame;

// CCamera layout offsets verified from DWARF:
//   +8    : CPlaceable::m_matrix (CMatrix)
//   +2032 : m_pRwCamera (RwCamera*)
inline CMatrix* camera_matrix(void* the_camera) {
    return reinterpret_cast<CMatrix*>(static_cast<uint8_t*>(the_camera) + 8);
}

inline RwCamera* camera_rw_camera(void* the_camera) {
    return *reinterpret_cast<RwCamera**>(static_cast<uint8_t*>(the_camera) + 2032);
}

// rw::Camera::object.object.parent (RwFrame*) at offset +8
inline RwFrame* rw_camera_get_frame(RwCamera* cam) {
    if (!cam) return nullptr;
    return *reinterpret_cast<RwFrame**>(reinterpret_cast<uint8_t*>(cam) + 8);
}

// rw::Frame::matrix (RwMatrix) at offset +48 verified from DWARF
inline RwMatrix* rw_frame_get_matrix(RwFrame* frame) {
    if (!frame) return nullptr;
    return reinterpret_cast<RwMatrix*>(reinterpret_cast<uint8_t*>(frame) + 48);
}

struct Re3Symbols {
    void* the_camera = nullptr;
    float* ms_fTimeStep = nullptr;
    unsigned long* streaming_memory_available = nullptr;
    unsigned long* streaming_memory_used = nullptr;

    void (*glfwSwapBuffers)(void* window) = nullptr;
    int (*glfwGetKey)(void* window, int key) = nullptr;
    void (*glfwGetFramebufferSize)(void* window, int* width, int* height) = nullptr;
    void (*glfwGetWindowSize)(void* window, int* width, int* height) = nullptr;
    void (*glfwGetCursorPos)(void* window, double* xpos, double* ypos) = nullptr;
    int (*glfwGetMouseButton)(void* window, int button) = nullptr;
    void (*glfwSetInputMode)(void* window, int mode, int value) = nullptr;

    void (*CGame_Process)() = nullptr;
    void (*CCamera_Process)(void* self) = nullptr;
    void (*CPlayerPed_ProcessControl)(void* self) = nullptr;
    void (*CStreaming_StreamZoneModels)(const CVector& pos) = nullptr;
    RwFrame* (*RwFrameUpdateObjects)(RwFrame* frame) = nullptr;
    CVector (*FindPlayerCoors)() = nullptr;
    const CVector& (*FindPlayerCentreOfWorld)(int player_id) = nullptr;
    bool (*CWorld_ProcessLineOfSight)(
        const CVector& origin, const CVector& target,
        CColPoint& out_point, CEntity*& out_entity,
        bool buildings, bool vehicles, bool peds,
        bool objects, bool dummies, bool see_through, bool ignore_some
    ) = nullptr;

    bool load() {
        uintptr_t base = 0;
        dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* data) -> int {
            if (!info->dlpi_name || info->dlpi_name[0] == '\0') {
                *reinterpret_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
                return 1;
            }
            return 0;
        }, &base);

        int fd = open("/proc/self/exe", O_RDONLY);
        if (fd < 0) return false;

        struct stat st{};
        if (fstat(fd, &st) != 0 || st.st_size <= 0) {
            close(fd);
            return false;
        }

        void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map == MAP_FAILED) return false;

        std::unordered_map<std::string, uintptr_t> table;
        const auto* ptr = static_cast<const uint8_t*>(map);
        const auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(ptr);
        const auto* shdrs = reinterpret_cast<const Elf64_Shdr*>(ptr + ehdr->e_shoff);

        for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
            if (shdrs[i].sh_type != SHT_SYMTAB) continue;
            const auto* syms = reinterpret_cast<const Elf64_Sym*>(ptr + shdrs[i].sh_offset);
            size_t count = shdrs[i].sh_size / sizeof(Elf64_Sym);
            const char* strtab = reinterpret_cast<const char*>(ptr + shdrs[shdrs[i].sh_link].sh_offset);
            for (size_t j = 0; j < count; ++j) {
                if (syms[j].st_name == 0 || syms[j].st_value == 0) continue;
                table[strtab + syms[j].st_name] = base + static_cast<uintptr_t>(syms[j].st_value);
            }
        }
        munmap(map, st.st_size);

        auto resolve = [&]<typename T>(T& out, const char* name, bool required = true) -> bool {
            auto it = table.find(name);
            if (it == table.end()) {
                if (required) {
                    std::fprintf(stderr, "[re3-fpv] ERROR: symbol not found: %s\n", name);
                }
                return !required;
            }
            out = reinterpret_cast<T>(it->second);
            return true;
        };

        bool ok = true;
        ok &= resolve(the_camera, "TheCamera");
        ok &= resolve(ms_fTimeStep, "_ZN6CTimer12ms_fTimeStepE");
        resolve(streaming_memory_available, "_ZN10CStreaming18ms_memoryAvailableE", false);
        resolve(streaming_memory_used, "_ZN10CStreaming13ms_memoryUsedE", false);
        ok &= resolve(glfwSwapBuffers, "glfwSwapBuffers");
        ok &= resolve(glfwGetKey, "glfwGetKey");
        ok &= resolve(glfwGetFramebufferSize, "glfwGetFramebufferSize");
        resolve(glfwGetWindowSize, "glfwGetWindowSize", false);
        resolve(glfwGetCursorPos, "glfwGetCursorPos", false);
        resolve(glfwGetMouseButton, "glfwGetMouseButton", false);
        resolve(glfwSetInputMode, "glfwSetInputMode", false);
        ok &= resolve(CGame_Process, "_ZN5CGame7ProcessEv");
        ok &= resolve(CCamera_Process, "_ZN7CCamera7ProcessEv");
        ok &= resolve(CPlayerPed_ProcessControl, "_ZN10CPlayerPed14ProcessControlEv");
        resolve(CStreaming_StreamZoneModels, "_ZN10CStreaming16StreamZoneModelsERK7CVector", false);
        ok &= resolve(RwFrameUpdateObjects, "_Z20RwFrameUpdateObjectsPN2rw5FrameE");
        ok &= resolve(FindPlayerCoors, "_Z15FindPlayerCoorsv");
        ok &= resolve(FindPlayerCentreOfWorld, "_Z23FindPlayerCentreOfWorldi");
        ok &= resolve(CWorld_ProcessLineOfSight, "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbb");
        return ok;
    }
};
