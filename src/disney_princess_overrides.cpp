#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <vector>

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"

namespace
{
    constexpr uint32_t kSceCdSyncAddress = 0x00322960u;
    constexpr uint32_t kSceCdDiskReadyAddress = 0x00323178u;
    constexpr uint32_t kSceCdReadAddress = 0x00323398u;
    constexpr uint32_t kAllocatorAddress = 0x001D2B20u;
    constexpr uint32_t kAllocatorLoopAddress = 0x001D2C18u;
    constexpr uint32_t kBootStateAddress = 0x001083E8u;
    constexpr uint32_t kPostLegalStateAddress = 0x001085C8u;
    constexpr uint32_t kFrameServiceAddress = 0x0025FC40u;
    constexpr uint32_t kSifSendCmdAddress = 0x00311978u;
    constexpr uint32_t kSceSifSendCmdAddress = 0x00311AB0u;
    constexpr uint32_t kSceSifCallRpcAddress = 0x003126B0u;
    constexpr uint32_t kSceSifLoadModuleBufferAddress = 0x00317130u;
    constexpr uint32_t kSetupHeapHandlerAddress = 0x001001E4u;
    constexpr uint32_t kEndOfHeapHandlerAddress = 0x0030E984u;
    constexpr uint32_t kSetupHeapSyscall = 0x3Du;
    constexpr uint32_t kEndOfHeapSyscall = 0x3Eu;
    constexpr uint32_t kDefaultGameHeapLimit = 0x01F00000u;
    constexpr uint32_t kSifCommandHeaderSize = 16u;
    constexpr uint32_t kSifCommandMaxPacketSize = 112u;
    constexpr uint32_t kSifCommandMaxExtraSize = 0x00FFFFFFu;
    constexpr uint32_t kSifStrmStateAddress = 0x0006A934u;
    constexpr int32_t kSceCdComplete = 2;
    constexpr int32_t kSceCdNotReady = 6;

    std::atomic_bool g_cdReadPending{false};
    std::atomic_uint32_t g_allocatorEntryTraceCount{0u};
    std::atomic_uint32_t g_allocatorLoopTraceCount{0u};
    std::atomic_uint32_t g_cdReadTraceCount{0u};
    std::atomic_uint32_t g_cdSyncTraceCount{0u};
    std::atomic_uint32_t g_rpcTraceCount{0u};
    std::atomic_uint32_t g_bootStateTraceCount{0u};
    std::atomic_uint32_t g_lastBootState{std::numeric_limits<uint32_t>::max()};
    std::atomic_uint32_t g_postLegalStateTraceCount{0u};
    std::atomic_uint32_t g_lastPostLegalState{
        std::numeric_limits<uint32_t>::max()};
    std::atomic_uint32_t g_schedulerTraceCount{0u};
    std::atomic_uint32_t g_schedulerTraceTarget{120u};
    std::atomic_uint32_t g_gameHeapLimit{kDefaultGameHeapLimit};
    PS2Runtime::RecompiledFunction g_allocatorFunction = nullptr;
    PS2Runtime::RecompiledFunction g_bootStateFunction = nullptr;
    PS2Runtime::RecompiledFunction g_postLegalStateFunction = nullptr;
    PS2Runtime::RecompiledFunction g_frameServiceFunction = nullptr;
    PS2Runtime::RecompiledFunction g_sceSifCallRpcFunction = nullptr;

    void disneySetupHeap(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gameHeapBase = (GPR_U32(ctx, 4) + 0xFu) & ~0xFu;
        const uint32_t requestedSize = GPR_U32(ctx, 5);
        uint32_t gameHeapLimit = kDefaultGameHeapLimit;
        if (requestedSize != 0u && requestedSize != 0xFFFFFFFFu)
        {
            const uint64_t requestedLimit =
                static_cast<uint64_t>(gameHeapBase) + requestedSize;
            gameHeapLimit = static_cast<uint32_t>(
                std::min<uint64_t>(requestedLimit, kDefaultGameHeapLimit));
        }
        if (gameHeapLimit <= gameHeapBase)
        {
            gameHeapLimit = kDefaultGameHeapLimit;
        }

        const uint32_t hostHeapBase = runtime->guestHeapBase();
        if (hostHeapBase >= gameHeapBase)
        {
            std::cerr << "[DisneyPrincess] no safe pre-game HLE heap range: base=0x"
                      << std::hex << hostHeapBase << " gameBase=0x" << gameHeapBase
                      << std::dec << std::endl;
            setReturnU32(ctx, 0u);
            ctx->pc = GPR_U32(ctx, 31);
            return;
        }

        runtime->configureGuestHeap(hostHeapBase, gameHeapBase);
        g_gameHeapLimit.store(gameHeapLimit, std::memory_order_relaxed);
        setReturnU32(ctx, gameHeapBase);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void disneyEndOfHeap(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnU32(ctx, g_gameHeapLimit.load(std::memory_order_relaxed));
        ctx->pc = GPR_U32(ctx, 31);
    }

    bool allocatorTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_ALLOCATOR");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    bool iopTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_IOP");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    bool cdTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_CD");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    bool rpcTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_RPC");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    bool bootStateTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_BOOT_STATE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    bool schedulerTraceEnabled()
    {
        const char *value = std::getenv("DPRE_TRACE_SCHEDULER");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    uint32_t schedulerTraceTarget()
    {
        const char *const value = std::getenv("DPRE_TRACE_SCHEDULER");
        if (value == nullptr || value[0] == '\0' || value[0] == '0' ||
            (value[0] == '1' && value[1] == '\0'))
        {
            return 120u;
        }

        char *end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 0);
        if (end == value || *end != '\0' ||
            parsed > std::numeric_limits<uint32_t>::max())
        {
            std::cerr << "[DisneyPrincess:scheduler] invalid trace index '"
                      << value << "'; using 120" << std::endl;
            return 120u;
        }
        return static_cast<uint32_t>(parsed);
    }

    bool autoBootInputEnabled()
    {
        const char *value = std::getenv("DPRE_AUTO_SKIP_BOOT");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }

    void traceBootState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_bootStateTraceCount.fetch_add(1u, std::memory_order_relaxed);
        const uint32_t object = GPR_U32(ctx, 4);
        const bool validObject = object <= PS2_RAM_SIZE - 0x6C8u;
        const uint32_t stateBefore =
            validObject ? READ32(object + 0xD4u) : 0xFFFFFFFFu;
        const uint32_t timerBefore =
            validObject ? READ32(object + 0x6C4u) : 0u;

        g_bootStateFunction(rdram, ctx, runtime);

        const uint32_t stateAfter =
            validObject ? READ32(object + 0xD4u) : 0xFFFFFFFFu;
        const uint32_t timerAfter =
            validObject ? READ32(object + 0x6C4u) : 0u;
        const uint32_t previousState =
            g_lastBootState.exchange(stateAfter, std::memory_order_relaxed);
        if (traceIndex < 16u || stateBefore != stateAfter ||
            previousState != stateAfter || traceIndex % 120u == 0u)
        {
            std::cerr << "[DisneyPrincess:boot-state]"
                      << " index=" << traceIndex
                      << " object=0x" << std::hex << object
                      << " state=" << std::dec << stateBefore
                      << "->" << stateAfter
                      << " timerBits=0x" << std::hex << timerBefore
                      << "->0x" << timerAfter
                      << " delta=" << std::dec << ctx->f[12]
                      << " result=0x" << std::hex << GPR_U32(ctx, 2)
                      << std::dec << std::endl;
        }
    }

    void tracePostLegalState(uint8_t *rdram, R5900Context *ctx,
                             PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_postLegalStateTraceCount.fetch_add(1u, std::memory_order_relaxed);
        const uint32_t object = GPR_U32(ctx, 4);
        const bool validObject =
            object != 0u && object <= PS2_RAM_SIZE - 0x6F0u;
        const uint32_t stateBefore =
            validObject ? READ32(object + 0xD4u) : 0xFFFFFFFFu;
        const float delta = ctx->f[12];

        g_postLegalStateFunction(rdram, ctx, runtime);

        const uint32_t stateAfter =
            validObject ? READ32(object + 0xD4u) : 0xFFFFFFFFu;
        const uint32_t effect =
            validObject ? READ32(object + 0xB8u) : 0u;
        const bool validEffect =
            effect != 0u && effect <= PS2_RAM_SIZE - 0x38u;
        const uint32_t effectObject =
            validEffect ? READ32(effect + 0x8u) : 0u;
        const bool validEffectObject =
            effectObject != 0u &&
            effectObject <= PS2_RAM_SIZE - 0x150u;
        const uint32_t previousState = g_lastPostLegalState.exchange(
            stateAfter, std::memory_order_relaxed);
        if (traceIndex < 16u || stateBefore != stateAfter ||
            previousState != stateAfter || traceIndex % 120u == 0u)
        {
            std::cerr << "[DisneyPrincess:post-legal-state]"
                      << " index=" << traceIndex
                      << " object=0x" << std::hex << object
                      << " state=" << std::dec << stateBefore
                      << "->" << stateAfter
                      << " delta=" << delta
                      << " effect=0x" << std::hex << effect
                      << " effectObject=0x" << effectObject
                      << " effectState=" << std::dec
                      << (validEffectObject ? READ32(effectObject) :
                          0xFFFFFFFFu)
                      << " effectTimerBits={0x" << std::hex
                      << (validEffect ? READ32(effect + 0xCu) : 0u)
                      << ",0x"
                      << (validEffect ? READ32(effect + 0x10u) : 0u)
                      << "} effectResult=" << std::dec
                      << (validEffect ?
                          static_cast<int16_t>(READ16(effect + 0x34u)) :
                          -1)
                      << " effectIndex=" << std::dec
                      << (validObject ? READ32(object + 0x4A8u) : 0u)
                      << " flags={0x" << std::hex
                      << (validObject ? READ32(object + 0x90u) : 0u)
                      << ",0x"
                      << (validObject ? READ32(object + 0x94u) : 0u)
                      << "} timerBits=0x"
                      << (validObject ? READ32(object + 0x6ECu) : 0u)
                      << std::dec << std::endl;
        }
    }

    void traceScheduler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_schedulerTraceCount.fetch_add(1u, std::memory_order_relaxed);
        if (autoBootInputEnabled())
        {
            constexpr uint16_t kCrossAndStartPressed =
                static_cast<uint16_t>(0xFFFFu & ~(1u << 14u) & ~(1u << 3u));
            const uint32_t pulsePhase = traceIndex % 120u;
            if (pulsePhase >= 30u && pulsePhase < 34u)
            {
                ps2_stubs::setPadOverrideState(
                    kCrossAndStartPressed, 0x80u, 0x80u, 0x80u, 0x80u);
            }
            else
            {
                ps2_stubs::clearPadOverrideState();
            }
        }

        if (traceIndex ==
            g_schedulerTraceTarget.load(std::memory_order_relaxed))
        {
            std::cerr << "[DisneyPrincess:scheduler] current"
                      << " pc=0x" << std::hex << ctx->pc
                      << " ra=0x" << GPR_U32(ctx, 31)
                      << " sp=0x" << GPR_U32(ctx, 29)
                      << " a0=0x" << GPR_U32(ctx, 4)
                      << " a1=0x" << GPR_U32(ctx, 5)
                      << " s0=0x" << GPR_U32(ctx, 16)
                      << " s1=0x" << GPR_U32(ctx, 17)
                      << " s2=0x" << GPR_U32(ctx, 18)
                      << " s3=0x" << GPR_U32(ctx, 19)
                      << std::dec << std::endl;
            const std::vector<PS2Runtime::GuestBranchDebugEntry> branches =
                runtime->guestBranchDebugHistory();
            std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>>
                branchTargetCounts;
            std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>>
                indirectTargetCounts;
            for (const PS2Runtime::GuestBranchDebugEntry &branch : branches)
            {
                auto &count = branchTargetCounts[branch.targetPc];
                ++count.first;
                count.second = branch.sourcePc;
                if (branch.kind ==
                    PS2Runtime::GuestBranchKind::IndirectCall)
                {
                    auto &indirectCount =
                        indirectTargetCounts[branch.targetPc];
                    ++indirectCount.first;
                    indirectCount.second = branch.sourcePc;
                }
            }
            std::vector<std::pair<uint32_t,
                                  std::pair<uint32_t, uint32_t>>>
                sortedBranchTargets(branchTargetCounts.begin(),
                                    branchTargetCounts.end());
            std::sort(sortedBranchTargets.begin(),
                      sortedBranchTargets.end(),
                      [](const auto &left, const auto &right)
                      {
                          return left.second.first > right.second.first;
                      });
            std::cerr << "[DisneyPrincess:scheduler] guest-call-targets"
                      << " history=" << branches.size()
                      << " unique=" << sortedBranchTargets.size()
                      << std::endl;
            for (size_t index = 0u;
                 index < std::min<size_t>(sortedBranchTargets.size(), 32u);
                 ++index)
            {
                const auto &[target, countAndSource] =
                    sortedBranchTargets[index];
                std::cerr << "[DisneyPrincess:scheduler] guest-call-target"
                          << " rank=" << index
                          << " target=0x" << std::hex << target
                          << " source=0x" << countAndSource.second
                          << std::dec << " count=" << countAndSource.first
                          << std::endl;
            }
            std::vector<std::pair<uint32_t,
                                  std::pair<uint32_t, uint32_t>>>
                sortedIndirectTargets(indirectTargetCounts.begin(),
                                      indirectTargetCounts.end());
            std::sort(sortedIndirectTargets.begin(),
                      sortedIndirectTargets.end(),
                      [](const auto &left, const auto &right)
                      {
                          return left.second.first > right.second.first;
                      });
            for (size_t index = 0u;
                 index < std::min<size_t>(sortedIndirectTargets.size(), 64u);
                 ++index)
            {
                const auto &[target, countAndSource] =
                    sortedIndirectTargets[index];
                std::cerr << "[DisneyPrincess:scheduler] guest-indirect-target"
                          << " rank=" << index
                          << " target=0x" << std::hex << target
                          << " source=0x" << countAndSource.second
                          << std::dec << " count=" << countAndSource.first
                          << std::endl;
            }
            const size_t recentStart =
                branches.size() > 64u ? branches.size() - 64u : 0u;
            for (size_t index = recentStart; index < branches.size(); ++index)
            {
                const PS2Runtime::GuestBranchDebugEntry &branch =
                    branches[index];
                std::cerr << "[DisneyPrincess:scheduler] guest-call-recent"
                          << " index=" << index
                          << " source=0x" << std::hex << branch.sourcePc
                          << " target=0x" << branch.targetPc
                          << " return=0x" << branch.fallthroughPc
                          << std::dec << " kind="
                          << static_cast<uint32_t>(branch.kind)
                          << std::endl;
            }
            const EeKernelSnapshot scheduler = runtime->eeScheduler().snapshot();
            std::cerr << "[DisneyPrincess:scheduler] running="
                      << scheduler.runningThreadId
                      << " threads=" << scheduler.threads.size()
                      << " eeCycle=" << scheduler.eeCycle << std::endl;
            for (const EeThreadSnapshot &thread : scheduler.threads)
            {
                std::cerr << "[DisneyPrincess:scheduler] thread=" << thread.id
                          << " status=" << static_cast<uint32_t>(thread.status)
                          << " wait=" << static_cast<uint32_t>(thread.waitReason)
                          << " waitId=" << thread.waitId
                          << " priority=" << thread.currentPriority
                          << " pc=0x" << std::hex << thread.pc
                          << " ra=0x" << thread.ra
                          << " sp=0x" << thread.sp
                          << " entry=0x" << thread.entry
                          << " stack=0x" << thread.stack
                          << "/0x" << thread.stackSize
                          << std::dec << std::endl;
                std::cerr << "[DisneyPrincess:scheduler] thread=" << thread.id
                          << " stack.words=";
                for (uint32_t offset = 0u; offset < 0x40u; offset += 4u)
                {
                    std::cerr << (offset == 0u ? "{0x" : ",0x")
                              << std::hex << READ32(ADD32(thread.sp, offset));
                }
                std::cerr << "}" << std::dec << std::endl;
            }
            for (const EeSemaphoreSnapshot &semaphore : scheduler.semaphores)
            {
                std::cerr << "[DisneyPrincess:scheduler] semaphore="
                          << semaphore.id
                          << " count=" << semaphore.count
                          << " max=" << semaphore.maxCount
                          << " waiters=" << semaphore.waiters << std::endl;
            }

            const ps2x::iop::DebugSnapshot iop = runtime->iopDebugSnapshot();
            std::cerr << "[DisneyPrincess:scheduler] iopCycles="
                      << iop.emulatorCycles
                      << " iopInstructions=" << iop.emulatorInstructions
                      << " iopModules=" << iop.emulatorLoadedModules
                      << " iopThreads=" << iop.emulatorThreads
                      << " iopRpcServers=" << iop.emulatorRpcServers
                      << std::endl;
            for (const ps2x::iop::DebugThread &thread : iop.threads)
            {
                std::cerr << "[DisneyPrincess:scheduler] iopThread="
                          << thread.id
                          << " state=" << thread.state
                          << " priority=" << thread.priority
                          << " pc=0x" << std::hex << thread.pc
                          << " ra=0x" << thread.ra
                          << " sp=0x" << thread.sp
                          << " entry=0x" << thread.entry
                          << std::dec << " waitId=" << thread.waitId
                          << " waitBits=0x" << std::hex << thread.waitBits
                          << " wakeCycle=0x" << thread.wakeCycle
                          << std::dec << std::endl;
            }
            for (const std::string &diagnostic : iop.diagnostics)
            {
                std::cerr << "[DisneyPrincess:scheduler] IOP " << diagnostic
                          << std::endl;
            }
            const ps2_stubs::PadDebugSnapshot pad =
                ps2_stubs::getPadDebugSnapshot();
            for (size_t port = 0u;
                 port < ps2_stubs::kPadDebugPortCount;
                 ++port)
            {
                const ps2_stubs::PadDebugPortSnapshot &padPort =
                    pad.ports[port][0];
                std::cerr << "[DisneyPrincess:scheduler] pad port=" << port
                          << " open=" << (padPort.open ? 1 : 0)
                          << " reads=" << padPort.readCount
                          << " ok=" << (padPort.lastReadOk ? 1 : 0)
                          << " backend=" << (padPort.lastUsedBackend ? 1 : 0)
                          << " override=" << (padPort.lastUsedOverride ? 1 : 0)
                          << " buttons=0x" << std::hex
                          << padPort.lastButtons
                          << " readAddr=0x" << padPort.lastReadDataAddr
                          << std::dec << std::endl;
            }
            std::array<uint32_t, 16> sifStrmState{};
            if (runtime->readIopMemory(kSifStrmStateAddress,
                                       sifStrmState.data(),
                                       sizeof(sifStrmState)))
            {
                std::cerr << "[DisneyPrincess:scheduler] SIFSTRM state=";
                for (size_t i = 0u; i < sifStrmState.size(); ++i)
                {
                    std::cerr << (i == 0u ? "{0x" : ",0x")
                              << std::hex << sifStrmState[i];
                }
                std::cerr << "}" << std::dec << std::endl;
            }

            PS2Memory &memory = runtime->memory();
            const GSRegisters &registers = memory.gs();
            const GSDebugSnapshot gs = runtime->gs().getDebugSnapshot();
            const std::vector<GSDebugHistoryEntry> history =
                runtime->gs().getDebugHistory();
            std::vector<uint8_t> hostPixels;
            uint32_t hostWidth = 0u;
            uint32_t hostHeight = 0u;
            const bool copiedHostFrame =
                runtime->gs().copyLatchedHostPresentationFrame(
                    hostPixels, hostWidth, hostHeight);
            uint64_t hostNonBlack = 0u;
            uint32_t hostColorOr = 0u;
            uint32_t hostMinX = hostWidth;
            uint32_t hostMinY = hostHeight;
            uint32_t hostMaxX = 0u;
            uint32_t hostMaxY = 0u;
            for (uint32_t y = 0u; y < hostHeight; ++y)
            {
                for (uint32_t x = 0u; x < hostWidth; ++x)
                {
                    const size_t offset =
                        (static_cast<size_t>(y) * hostWidth + x) * 4u;
                    if (offset + 4u > hostPixels.size())
                    {
                        break;
                    }
                    uint32_t color = 0u;
                    std::memcpy(&color, hostPixels.data() + offset,
                                sizeof(color));
                    hostColorOr |= color;
                    if ((color & 0x00FFFFFFu) != 0u)
                    {
                        ++hostNonBlack;
                        hostMinX = std::min(hostMinX, x);
                        hostMinY = std::min(hostMinY, y);
                        hostMaxX = std::max(hostMaxX, x);
                        hostMaxY = std::max(hostMaxY, y);
                    }
                }
            }
            size_t gifTagEvents = 0u;
            size_t registerEvents = 0u;
            size_t drawEvents = 0u;
            size_t transferEvents = 0u;
            size_t presentEvents = 0u;
            const GSDebugHistoryEntry *lastDraw = nullptr;
            std::vector<size_t> drawFbpCounts(0x200u, 0u);
            for (const GSDebugHistoryEntry &entry : history)
            {
                switch (entry.kind)
                {
                case GSDebugEventKind::GifTag:
                    ++gifTagEvents;
                    break;
                case GSDebugEventKind::Register:
                    ++registerEvents;
                    break;
                case GSDebugEventKind::Draw:
                    ++drawEvents;
                    lastDraw = &entry;
                    ++drawFbpCounts[entry.frame.fbp & 0x1FFu];
                    break;
                case GSDebugEventKind::Transfer:
                    ++transferEvents;
                    break;
                case GSDebugEventKind::Present:
                    ++presentEvents;
                    break;
                }
            }
            std::cerr << "[DisneyPrincess:scheduler] GS"
                      << " pmode=0x" << std::hex << registers.pmode
                      << " dispfb1=0x" << registers.dispfb1
                      << " display1=0x" << registers.display1
                      << " frame0.fbp=0x" << gs.ctx[0].frame.fbp
                      << " frame0.fbw=0x" << gs.ctx[0].frame.fbw
                      << " frame0.psm=0x"
                      << static_cast<uint32_t>(gs.ctx[0].frame.psm)
                      << " frame1.fbp=0x" << gs.ctx[1].frame.fbp
                      << " frame1.fbw=0x" << gs.ctx[1].frame.fbw
                      << " frame1.psm=0x"
                      << static_cast<uint32_t>(gs.ctx[1].frame.psm)
                      << " hostFrame=" << std::dec
                      << (gs.hasHostPresentationFrame ? 1 : 0)
                      << " hostSize=" << gs.hostPresentationWidth
                      << "x" << gs.hostPresentationHeight
                      << " vsyncTick="
                      << runtime->eeScheduler().currentVSyncTick()
                      << " gifPackets=" << runtime->gs().nativePackedGIFPacketCount()
                      << " imageUploads=" << runtime->gs().nativeImageUploadCount()
                      << " history=" << history.size()
                      << std::endl;
            std::cerr << "[DisneyPrincess:scheduler] GS-host-pixels"
                      << " copied=" << (copiedHostFrame ? 1 : 0)
                      << " size=" << hostWidth << "x" << hostHeight
                      << " nonBlack=" << hostNonBlack
                      << " colorOr=0x" << std::hex << hostColorOr
                      << std::dec;
            if (hostNonBlack != 0u)
            {
                std::cerr << " bounds={" << hostMinX << "," << hostMinY
                          << " -> " << hostMaxX << "," << hostMaxY << "}";
            }
            std::cerr << std::endl;
            std::cerr << "[DisneyPrincess:scheduler] GS-events"
                      << " gifTags=" << gifTagEvents
                      << " registers=" << registerEvents
                      << " draws=" << drawEvents
                      << " transfers=" << transferEvents
                      << " presents=" << presentEvents
                      << " preferred=" << (gs.hasPreferredDisplaySource ? 1 : 0)
                      << " preferred.fbp=0x" << std::hex
                      << gs.preferredDisplaySourceFrame.fbp
                      << " preferred.fbw=0x"
                      << gs.preferredDisplaySourceFrame.fbw
                      << " preferred.psm=0x"
                      << static_cast<uint32_t>(gs.preferredDisplaySourceFrame.psm)
                      << " preferred.dest=0x" << gs.preferredDisplayDestFbp
                      << " host.display=0x" << gs.hostPresentationDisplayFbp
                      << " host.source=0x" << gs.hostPresentationSourceFbp
                      << " transfer=" << std::dec << gs.transferCopiedPixels
                      << "/" << gs.transferTotalPixels
                      << std::endl;
            std::cerr << "[DisneyPrincess:scheduler] GS-draw-fbps={";
            bool firstFbp = true;
            for (size_t fbp = 0u; fbp < drawFbpCounts.size(); ++fbp)
            {
                if (drawFbpCounts[fbp] == 0u)
                {
                    continue;
                }
                std::cerr << (firstFbp ? "0x" : ",0x") << std::hex << fbp
                          << ":" << std::dec << drawFbpCounts[fbp];
                firstFbp = false;
            }
            std::cerr << "}" << std::endl;
            if (lastDraw != nullptr)
            {
                std::cerr << "[DisneyPrincess:scheduler] GS-last-draw"
                          << " frame=" << lastDraw->frameIndex
                          << " vertices=" << lastDraw->vertexCount
                          << " xy={" << lastDraw->xMin << "," << lastDraw->yMin
                          << " -> " << lastDraw->xMax << "," << lastDraw->yMax
                          << "} z={" << lastDraw->zMin << " -> "
                          << lastDraw->zMax << "}"
                          << " alpha={" << static_cast<uint32_t>(lastDraw->aMin)
                          << " -> " << static_cast<uint32_t>(lastDraw->aMax)
                          << "} fbp=0x" << std::hex << lastDraw->frame.fbp
                          << " fbw=0x" << lastDraw->frame.fbw
                          << " psm=0x"
                          << static_cast<uint32_t>(lastDraw->frame.psm)
                          << " prim=" << std::dec
                          << static_cast<uint32_t>(lastDraw->prim.type)
                          << " tme=" << (lastDraw->prim.tme ? 1 : 0)
                          << " abe=" << (lastDraw->prim.abe ? 1 : 0)
                          << " fst=" << (lastDraw->prim.fst ? 1 : 0)
                          << " ctxt=" << (lastDraw->prim.ctxt ? 1 : 0)
                          << " tex.tbp=0x" << std::hex
                          << lastDraw->tex0.tbp0
                          << " tex.tbw=0x"
                          << static_cast<uint32_t>(lastDraw->tex0.tbw)
                          << " tex.psm=0x"
                          << static_cast<uint32_t>(lastDraw->tex0.psm)
                          << std::dec << std::endl;
            }

            const uint32_t candidateFbps[] = {
                0u,
                0x80u,
                0x100u,
                gs.ctx[0].frame.fbp,
                gs.ctx[1].frame.fbp,
            };
            for (size_t candidate = 0u;
                 candidate < sizeof(candidateFbps) / sizeof(candidateFbps[0]);
                 ++candidate)
            {
                bool alreadySampled = false;
                for (size_t previous = 0u; previous < candidate; ++previous)
                {
                    alreadySampled =
                        alreadySampled ||
                        candidateFbps[previous] == candidateFbps[candidate];
                }
                if (alreadySampled)
                {
                    continue;
                }

                uint32_t nonBlack = 0u;
                uint32_t colorOr = 0u;
                for (uint32_t y = 0u; y < 448u; y += 8u)
                {
                    for (uint32_t x = 0u; x < 512u; x += 8u)
                    {
                        const uint32_t color = runtime->gs().ReadVram(
                            0u, candidateFbps[candidate], 8u, x, y);
                        colorOr |= color;
                        if ((color & 0x00FFFFFFu) != 0u)
                        {
                            ++nonBlack;
                        }
                    }
                }
                std::cerr << "[DisneyPrincess:scheduler] GS-vram-sample"
                          << " fbp=0x" << std::hex << candidateFbps[candidate]
                          << " colorOr=0x" << colorOr
                          << " nonBlack=" << std::dec << nonBlack
                          << "/3584" << std::endl;
            }

            std::cerr << "[DisneyPrincess:scheduler] transport"
                      << " dmaStarts=" << memory.dmaStartCount()
                      << " gifCopies=" << memory.gifCopyCount()
                      << " gsWrites=" << memory.gsWriteCount()
                      << " vifWrites=" << memory.vifWriteCount()
                      << " vu1CodeGen=" << memory.getVU1CodeGeneration()
                      << " path3Masked=" << (memory.isPath3Masked() ? 1 : 0)
                      << std::endl;

            std::cerr << "[DisneyPrincess:scheduler] VIF1-DMA"
                      << " chcr=0x" << std::hex
                      << memory.readIORegister(0x10009000u)
                      << " madr=0x" << memory.readIORegister(0x10009010u)
                      << " qwc=0x" << memory.readIORegister(0x10009020u)
                      << " tadr=0x" << memory.readIORegister(0x10009030u)
                      << " asr0=0x" << memory.readIORegister(0x10009040u)
                      << " asr1=0x" << memory.readIORegister(0x10009050u)
                      << std::dec << std::endl;

            std::cerr << "[DisneyPrincess:scheduler] VIF1"
                      << " stat=0x" << std::hex
                      << memory.readIORegister(0x10003C00u)
                      << " code=0x" << memory.readIORegister(0x10003C80u)
                      << " cycle=0x" << memory.readIORegister(0x10003C40u)
                      << " mode=0x" << memory.readIORegister(0x10003C50u)
                      << " num=0x" << memory.readIORegister(0x10003C60u)
                      << " itops=0x" << memory.readIORegister(0x10003C90u)
                      << " base=0x" << memory.readIORegister(0x10003CA0u)
                      << " ofst=0x" << memory.readIORegister(0x10003CB0u)
                      << " tops=0x" << memory.readIORegister(0x10003CC0u)
                      << " itop=0x" << memory.readIORegister(0x10003CD0u)
                      << " top=0x" << memory.readIORegister(0x10003CE0u)
                      << std::dec << std::endl;

            const VU1State &vu1 = runtime->vu1().state();
            const VU1XgkickDebugState xgkick =
                runtime->vu1().xgkickDebugState();
            std::cerr << "[DisneyPrincess:scheduler] VU1"
                      << " pc=0x" << std::hex << vu1.pc
                      << " cycles=0x" << vu1.cycles
                      << " top=0x" << vu1.top
                      << " itop=0x" << vu1.itop
                      << " ebit=" << std::dec << (vu1.ebit ? 1 : 0)
                      << " haltDelay=" << (vu1.haltAfterDelaySlot ? 1 : 0)
                      << " stoppedD=" << (vu1.stoppedByD ? 1 : 0)
                      << " stoppedT=" << (vu1.stoppedByT ? 1 : 0)
                      << std::endl;
            std::cerr << "[DisneyPrincess:scheduler] VU1-vi=";
            for (size_t i = 0u; i < std::size(vu1.vi); ++i)
            {
                std::cerr << (i == 0u ? "{0x" : ",0x")
                          << std::hex << static_cast<uint32_t>(vu1.vi[i]);
            }
            std::cerr << "}" << std::dec << std::endl;
            std::cerr << "[DisneyPrincess:scheduler] XGKICK"
                      << " active=" << (xgkick.active ? 1 : 0)
                      << " source=0x" << std::hex << xgkick.sourceAddress
                      << " total=0x" << xgkick.totalBytes
                      << " copied=0x" << xgkick.copiedBytes
                      << " tagEnd=0x" << xgkick.currentTagEnd
                      << " credit=0x" << xgkick.cycleCredit
                      << " issueCycle=0x" << xgkick.issueCycle
                      << " tagEop=" << std::dec
                      << (xgkick.currentTagEop ? 1 : 0)
                      << std::endl;
            const uint8_t *const vuCode = memory.getVU1Code();
            for (int32_t delta = -0x30; delta <= 0x10; delta += 8)
            {
                const uint32_t address =
                    (vu1.pc + static_cast<uint32_t>(delta)) &
                    (PS2_VU1_CODE_SIZE - 1u);
                uint32_t lower = 0u;
                uint32_t upper = 0u;
                std::memcpy(&lower, vuCode + address, sizeof(lower));
                std::memcpy(&upper, vuCode + address + sizeof(lower),
                            sizeof(upper));
                std::cerr << "[DisneyPrincess:scheduler] VU1-code"
                          << " address=0x" << std::hex << address
                          << " upper=0x" << upper
                          << " lower=0x" << lower
                          << std::dec << std::endl;
            }
            if (xgkick.sourceAddress < PS2_VU1_DATA_SIZE)
            {
                const uint8_t *const vuData = memory.getVU1Data();
                for (int32_t delta = -8; delta <= 8; ++delta)
                {
                    const uint32_t address =
                        (xgkick.sourceAddress +
                         static_cast<uint32_t>(delta * 16)) &
                        (PS2_VU1_DATA_SIZE - 1u);
                    uint32_t words[4]{};
                    std::memcpy(words, vuData + address, sizeof(words));
                    std::cerr << "[DisneyPrincess:scheduler] VU1-data"
                              << " qword=0x" << std::hex << (address / 16u)
                              << " words={0x" << words[0]
                              << ",0x" << words[1]
                              << ",0x" << words[2]
                              << ",0x" << words[3]
                              << "}" << std::dec << std::endl;
                }
                uint32_t packetOffset = 0u;
                for (uint32_t tagIndex = 0u; tagIndex < 16u; ++tagIndex)
                {
                    uint64_t tag = 0u;
                    auto *const tagBytes =
                        reinterpret_cast<uint8_t *>(&tag);
                    for (uint32_t byte = 0u; byte < sizeof(tag); ++byte)
                    {
                        tagBytes[byte] =
                            vuData[(xgkick.sourceAddress + packetOffset + byte) &
                                   (PS2_VU1_DATA_SIZE - 1u)];
                    }
                    const uint32_t nloop =
                        static_cast<uint32_t>(tag & 0x7FFFu);
                    const bool eop = ((tag >> 15u) & 1u) != 0u;
                    const uint32_t format =
                        static_cast<uint32_t>((tag >> 58u) & 3u);
                    uint32_t nreg =
                        static_cast<uint32_t>((tag >> 60u) & 0xFu);
                    if (nreg == 0u)
                    {
                        nreg = 16u;
                    }
                    uint64_t payloadBytes = 0u;
                    if (format == 0u)
                    {
                        payloadBytes =
                            static_cast<uint64_t>(nloop) * nreg * 16u;
                    }
                    else if (format == 1u)
                    {
                        payloadBytes =
                            ((static_cast<uint64_t>(nloop) * nreg + 1u) &
                             ~1ull) *
                            8u;
                    }
                    else
                    {
                        payloadBytes = static_cast<uint64_t>(nloop) * 16u;
                    }
                    std::cerr << "[DisneyPrincess:scheduler] XGKICK-tag"
                              << " index=" << tagIndex
                              << " offset=0x" << std::hex << packetOffset
                              << " address=0x"
                              << ((xgkick.sourceAddress + packetOffset) &
                                  (PS2_VU1_DATA_SIZE - 1u))
                              << " lo=0x" << tag
                              << std::dec << " nloop=" << nloop
                              << " eop=" << (eop ? 1 : 0)
                              << " format=" << format
                              << " nreg=" << nreg
                              << " payload=0x" << std::hex << payloadBytes
                              << std::dec << std::endl;
                    const uint64_t next =
                        static_cast<uint64_t>(packetOffset) + 16u + payloadBytes;
                    if (eop || next > 0x10000u)
                    {
                        break;
                    }
                    packetOffset = static_cast<uint32_t>(next);
                }
            }

            const uint32_t renderGate = READ32(0x00406D64u);
            const uint32_t chainAddress = READ32(0x0039EBC0u);
            const uint32_t chainCursor = READ32(0x0039EBC8u);
            const uint32_t chainPhysical = chainAddress & 0x1FFFFFFFu;
            std::cerr << "[DisneyPrincess:scheduler] render"
                      << " gate=0x" << std::hex << renderGate
                      << " chain=0x" << chainAddress
                      << " cursor=0x" << chainCursor
                      << " bytes=0x" << SUB32(chainCursor, chainAddress)
                      << std::dec << std::endl;
            if (chainPhysical <= PS2_RAM_SIZE - 0x100u)
            {
                for (uint32_t offset = 0u; offset < 0x100u; offset += 0x20u)
                {
                    std::cerr << "[DisneyPrincess:scheduler] chain+0x"
                              << std::hex << offset << "={";
                    for (uint32_t word = 0u; word < 0x20u; word += 4u)
                    {
                        std::cerr << (word == 0u ? "0x" : ",0x")
                                  << READ32(chainPhysical + offset + word);
                    }
                    std::cerr << "}" << std::dec << std::endl;
                }
            }
        }

        g_frameServiceFunction(rdram, ctx, runtime);
    }

    void traceSceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_rpcTraceCount.fetch_add(1u, std::memory_order_relaxed);
        if (traceIndex < 32u)
        {
            const uint32_t sp = GPR_U32(ctx, 29);
            const uint32_t client = GPR_U32(ctx, 4);
            const uint32_t server = READ32(ADD32(client, 0x24u));
            const uint32_t normalizedServer = server & 0x1FFFFFFFu;
            std::cerr << "[DisneyPrincess:RPC] index=" << traceIndex
                      << " client=0x" << std::hex << client
                      << " fno=0x" << GPR_U32(ctx, 5)
                      << " mode=0x" << GPR_U32(ctx, 6)
                      << " send=0x" << GPR_U32(ctx, 7)
                      << " regs={0x" << GPR_U32(ctx, 8)
                      << ",0x" << GPR_U32(ctx, 9)
                      << ",0x" << GPR_U32(ctx, 10)
                      << ",0x" << GPR_U32(ctx, 11)
                      << "} stack={0x" << READ32(ADD32(sp, 0x10u))
                      << ",0x" << READ32(ADD32(sp, 0x14u))
                      << ",0x" << READ32(ADD32(sp, 0x18u))
                      << ",0x" << READ32(ADD32(sp, 0x1Cu))
                      << ",0x" << READ32(ADD32(sp, 0x20u))
                      << "} server=0x" << server;
            if (normalizedServer >= 0x10000u &&
                normalizedServer <= PS2_RAM_SIZE - 16u)
            {
                std::cerr << " server.words={0x" << READ32(normalizedServer)
                          << ",0x" << READ32(ADD32(normalizedServer, 4u))
                          << ",0x" << READ32(ADD32(normalizedServer, 8u))
                          << ",0x" << READ32(ADD32(normalizedServer, 12u))
                          << "}";
            }
            std::cerr << std::dec << std::endl;
        }

        g_sceSifCallRpcFunction(rdram, ctx, runtime);
    }

    void disneySifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t commandId = GPR_U32(ctx, 4);
        const uint32_t packetAddress = GPR_U32(ctx, 5);
        const uint32_t packetSize = GPR_U32(ctx, 6);
        const uint32_t source = GPR_U32(ctx, 7);
        const uint32_t destination = GPR_U32(ctx, 8);
        const uint32_t size = GPR_U32(ctx, 9);

        if (runtime == nullptr ||
            packetAddress == 0u ||
            packetSize < kSifCommandHeaderSize ||
            packetSize > kSifCommandMaxPacketSize)
        {
            setReturnS32(ctx, 0);
            return;
        }

        auto readEeRange = [rdram](uint32_t address,
                                   uint32_t byteCount,
                                   std::vector<uint8_t> &bytes) -> bool
        {
            if (byteCount == 0u)
            {
                bytes.clear();
                return true;
            }
            if (address >
                std::numeric_limits<uint32_t>::max() - (byteCount - 1u))
            {
                return false;
            }

            bytes.resize(byteCount);
            for (uint32_t i = 0u; i < byteCount; ++i)
            {
                const uint8_t *sourceByte = getConstMemPtr(rdram, address + i);
                if (sourceByte == nullptr)
                {
                    bytes.clear();
                    return false;
                }
                bytes[i] = *sourceByte;
            }
            return true;
        };

        std::vector<uint8_t> packet;
        if (!readEeRange(packetAddress, packetSize, packet))
        {
            setReturnS32(ctx, 0);
            return;
        }

        if (size != 0u && source != 0u && destination != 0u)
        {
            if (size > kSifCommandMaxExtraSize ||
                !runtime->isIopMemoryRange(destination, size))
            {
                setReturnS32(ctx, 0);
                return;
            }

            std::vector<uint8_t> payload;
            if (!readEeRange(source, size, payload) ||
                !runtime->writeIopMemory(
                    destination, payload.data(), payload.size()))
            {
                setReturnS32(ctx, 0);
                return;
            }
        }
        else if (size != 0u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        const uint32_t sizeWord = packetSize | (size << 8u);
        std::memcpy(packet.data(), &sizeWord, sizeof(sizeWord));
        std::memcpy(packet.data() + 4u, &destination, sizeof(destination));
        std::memcpy(packet.data() + 8u, &commandId, sizeof(commandId));
        WRITE32(packetAddress, sizeWord);
        WRITE32(ADD32(packetAddress, 4u), destination);
        WRITE32(ADD32(packetAddress, 8u), commandId);

        const bool delivered = runtime->handleIopSifCommand(
            rdram, ctx, commandId, packet.data(), packet.size());
        if (!delivered)
        {
            std::cerr << "[DisneyPrincess] IOP rejected SIF command"
                      << " cid=0x" << std::hex << commandId
                      << " packetSize=0x" << packetSize
                      << " extraDestination=0x" << destination
                      << " extraSize=0x" << size
                      << std::dec << std::endl;
            setReturnS32(ctx, 0);
            return;
        }

        if (rpcTraceEnabled())
        {
            std::cerr << "[DisneyPrincess:RPC] delivered SIF command"
                      << " cid=0x" << std::hex << commandId
                      << " packetSize=0x" << packetSize
                      << " extraDestination=0x" << destination
                      << " extraSize=0x" << size
                      << " packetWords=";
            const size_t packetWords =
                std::min<size_t>(packet.size() / sizeof(uint32_t), 7u);
            for (size_t i = 0u; i < packetWords; ++i)
            {
                uint32_t word = 0u;
                std::memcpy(&word,
                            packet.data() + i * sizeof(uint32_t),
                            sizeof(word));
                std::cerr << (i == 0u ? "{0x" : ",0x") << word;
            }
            std::cerr << "}" << std::dec << std::endl;
        }
        setReturnS32(ctx, 1);
    }

    void logHeapState(uint8_t *rdram,
                      R5900Context *ctx,
                      PS2Runtime *runtime,
                      const char *phase)
    {
        const uint32_t heap = READ32(0x003A65DCu);
        std::cerr << "[DisneyPrincess:IOP] phase=" << phase
                  << " heap=0x" << std::hex << heap
                  << " next=0x" << READ32(ADD32(heap, 0x18u))
                  << " limit=0x" << READ32(ADD32(heap, 0x24u))
                  << " used=0x" << READ32(ADD32(heap, 0x28u))
                  << " free=0x" << READ32(ADD32(heap, 0x34u))
                  << std::dec << std::endl;
    }

    void traceSceSifLoadModuleBuffer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t buffer = GPR_U32(ctx, 4);
        std::cerr << "[DisneyPrincess:IOP] sceSifLoadModuleBuffer"
                  << " buffer=0x" << std::hex << buffer
                  << " argSize=0x" << GPR_U32(ctx, 5)
                  << " argAddress=0x" << GPR_U32(ctx, 6)
                  << " words=0x" << READ32(buffer)
                  << ",0x" << READ32(ADD32(buffer, 4u))
                  << ",0x" << READ32(ADD32(buffer, 8u))
                  << ",0x" << READ32(ADD32(buffer, 12u))
                  << std::dec << std::endl;
        logHeapState(rdram, ctx, runtime, "before-module-buffer");
        ps2_syscalls::sceSifLoadModuleBuffer(rdram, ctx, runtime);
        logHeapState(rdram, ctx, runtime, "after-module-buffer");
        std::cerr << "[DisneyPrincess:IOP] module-buffer-result=" << GPR_S32(ctx, 2)
                  << std::endl;
    }

    void traceDisneyAllocator(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool loopResume = ctx->pc == kAllocatorLoopAddress;
        const uint32_t traceIndex = loopResume
                                        ? g_allocatorLoopTraceCount.fetch_add(1u, std::memory_order_relaxed)
                                        : g_allocatorEntryTraceCount.fetch_add(1u, std::memory_order_relaxed);
        const uint32_t traceLimit = loopResume ? 32u : 8u;
        if (traceIndex < traceLimit)
        {
            const uint32_t heap = GPR_U32(ctx, 16);
            const uint32_t cursor = GPR_U32(ctx, 8);
            std::cerr << "[DisneyPrincess:allocator] phase="
                      << (loopResume ? "loop" : "entry")
                      << " index=" << traceIndex
                      << " pc=0x" << std::hex << ctx->pc
                      << " ra=0x" << GPR_U32(ctx, 31)
                      << " a0=0x" << GPR_U32(ctx, 4)
                      << " a1=0x" << GPR_U32(ctx, 5)
                      << " a2=0x" << GPR_U32(ctx, 6)
                      << " t0=0x" << cursor
                      << " t4=0x" << GPR_U32(ctx, 12)
                      << " s0=0x" << heap
                      << " heap.next=0x" << READ32(ADD32(heap, 0x18u))
                      << " heap.limit=0x" << READ32(ADD32(heap, 0x24u))
                      << " heap.used=0x" << READ32(ADD32(heap, 0x28u))
                      << " heap.free=0x" << READ32(ADD32(heap, 0x34u))
                      << " host.base=0x" << runtime->guestHeapBase()
                      << " host.end=0x" << runtime->guestHeapEnd()
                      << " host.limit=0x" << runtime->guestHeapLimit()
                      << " cursor.next=0x" << READ32(cursor)
                      << " cursor.size=0x" << READ32(ADD32(cursor, 4u))
                      << std::dec << std::endl;
        }

        g_allocatorFunction(rdram, ctx, runtime);
    }

    void disneySceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_cdReadTraceCount.fetch_add(1u, std::memory_order_relaxed);
        if (g_cdReadPending.load(std::memory_order_relaxed))
        {
            if (cdTraceEnabled() && traceIndex < 128u)
            {
                std::cerr << "[DisneyPrincess:CD] read busy"
                          << " index=" << traceIndex
                          << " lbn=0x" << std::hex << GPR_U32(ctx, 4)
                          << " sectors=0x" << GPR_U32(ctx, 5)
                          << " destination=0x" << GPR_U32(ctx, 6)
                          << std::dec << std::endl;
            }
            SET_GPR_S32(ctx, 2, 0);
            return;
        }

        ps2_stubs::sceCdRead(rdram, ctx, runtime);
        if (cdTraceEnabled() && traceIndex < 128u)
        {
            std::cerr << "[DisneyPrincess:CD] read"
                      << " index=" << traceIndex
                      << " lbn=0x" << std::hex << GPR_U32(ctx, 4)
                      << " sectors=0x" << GPR_U32(ctx, 5)
                      << " destination=0x" << GPR_U32(ctx, 6)
                      << std::dec << " result=" << GPR_S32(ctx, 2)
                      << std::endl;
        }
        if (GPR_S32(ctx, 2) != 0)
        {
            g_cdReadPending.store(true, std::memory_order_relaxed);
        }
    }

    void disneySceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t traceIndex =
            g_cdSyncTraceCount.fetch_add(1u, std::memory_order_relaxed);
        ps2_stubs::sceCdSync(rdram, ctx, runtime);
        if (cdTraceEnabled() && traceIndex < 128u)
        {
            std::cerr << "[DisneyPrincess:CD] sync"
                      << " index=" << traceIndex
                      << " mode=0x" << std::hex << GPR_U32(ctx, 4)
                      << std::dec << " result=" << GPR_S32(ctx, 2)
                      << std::endl;
        }
        if (GPR_S32(ctx, 2) == 0)
        {
            g_cdReadPending.store(false, std::memory_order_relaxed);
        }
    }

    void disneySceCdDiskReady(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        const int32_t status = g_cdReadPending.load(std::memory_order_relaxed)
                                   ? kSceCdNotReady
                                   : kSceCdComplete;
        SET_GPR_S32(ctx, 2, status);
    }

    void applyDisneyPrincessOverrides(PS2Runtime &runtime)
    {
        g_cdReadPending.store(false, std::memory_order_relaxed);
        g_cdReadTraceCount.store(0u, std::memory_order_relaxed);
        g_cdSyncTraceCount.store(0u, std::memory_order_relaxed);
        g_gameHeapLimit.store(kDefaultGameHeapLimit, std::memory_order_relaxed);
        bool replaced =
            runtime.replaceFunction(kSceCdReadAddress, disneySceCdRead) &&
            runtime.replaceFunction(kSceCdSyncAddress, disneySceCdSync) &&
            runtime.replaceFunction(kSceCdDiskReadyAddress, disneySceCdDiskReady) &&
            runtime.replaceFunction(kSifSendCmdAddress, disneySifSendCmd) &&
            runtime.replaceFunction(kSceSifSendCmdAddress, disneySifSendCmd) &&
            runtime.replaceFunction(kSetupHeapHandlerAddress, disneySetupHeap) &&
            runtime.replaceFunction(kEndOfHeapHandlerAddress, disneyEndOfHeap);

        if (replaced)
        {
            uint8_t *const rdram = runtime.memory().getRDRAM();
            runtime.setEeSyscallOverride(rdram,
                                         kSetupHeapSyscall,
                                         kSetupHeapHandlerAddress);
            runtime.setEeSyscallOverride(rdram,
                                         kEndOfHeapSyscall,
                                         kEndOfHeapHandlerAddress);
        }

        if (allocatorTraceEnabled())
        {
            g_allocatorEntryTraceCount.store(0u, std::memory_order_relaxed);
            g_allocatorLoopTraceCount.store(0u, std::memory_order_relaxed);
            g_allocatorFunction = runtime.lookupFunction(kAllocatorAddress);
            replaced = g_allocatorFunction != nullptr &&
                       runtime.replaceFunction(kAllocatorAddress, traceDisneyAllocator) &&
                       runtime.replaceFunction(kAllocatorLoopAddress, traceDisneyAllocator) &&
                       replaced;
        }

        if (bootStateTraceEnabled())
        {
            g_bootStateTraceCount.store(0u, std::memory_order_relaxed);
            g_lastBootState.store(std::numeric_limits<uint32_t>::max(),
                                  std::memory_order_relaxed);
            g_postLegalStateTraceCount.store(0u, std::memory_order_relaxed);
            g_lastPostLegalState.store(
                std::numeric_limits<uint32_t>::max(),
                std::memory_order_relaxed);
            g_bootStateFunction = runtime.lookupFunction(kBootStateAddress);
            g_postLegalStateFunction =
                runtime.lookupFunction(kPostLegalStateAddress);
            replaced = g_bootStateFunction != nullptr &&
                       g_postLegalStateFunction != nullptr &&
                       runtime.replaceFunction(kBootStateAddress,
                                               traceBootState) &&
                       runtime.replaceFunction(kPostLegalStateAddress,
                                               tracePostLegalState) &&
                       replaced;
        }

        if (iopTraceEnabled())
        {
            replaced = runtime.replaceFunction(kSceSifLoadModuleBufferAddress,
                                               traceSceSifLoadModuleBuffer) &&
                       replaced;
        }

        if (rpcTraceEnabled())
        {
            g_rpcTraceCount.store(0u, std::memory_order_relaxed);
            g_sceSifCallRpcFunction = runtime.lookupFunction(kSceSifCallRpcAddress);
            replaced = g_sceSifCallRpcFunction != nullptr &&
                       runtime.replaceFunction(kSceSifCallRpcAddress,
                                               traceSceSifCallRpc) &&
                       replaced;
        }

        if (schedulerTraceEnabled())
        {
            g_schedulerTraceCount.store(0u, std::memory_order_relaxed);
            g_schedulerTraceTarget.store(schedulerTraceTarget(),
                                         std::memory_order_relaxed);
            std::cerr << "[DisneyPrincess:scheduler] tracing frame-service call "
                      << g_schedulerTraceTarget.load(std::memory_order_relaxed)
                      << std::endl;
            runtime.gs().clearDebugHistory();
            runtime.gs().setDebugHistoryPaused(false);
            runtime.setGuestBranchDebugHistoryEnabled(true);
            g_frameServiceFunction = runtime.lookupFunction(kFrameServiceAddress);
            replaced = g_frameServiceFunction != nullptr &&
                       runtime.replaceFunction(kFrameServiceAddress,
                                               traceScheduler) &&
                       replaced;
        }

        if (!replaced)
        {
            std::cerr << "[DisneyPrincess] failed to install game-specific overrides"
                      << std::endl;
        }
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "Disney Princess: Enchanted Journey (USA)",
    "SLUS_216.60",
    0x00100008u,
    0x0AB87E75u,
    applyDisneyPrincessOverrides)
