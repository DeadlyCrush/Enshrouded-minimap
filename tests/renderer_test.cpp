// Exercise the production submission path with delayed GPU completion.
#include "../src/dllmain.cpp"
#include <iostream>

namespace
{
    constexpr std::int32_t TEST_ERROR = -4;
    struct FakeGpu
    {
        bool pending[2] = {};
        bool signaled[2] = { true, true };
        bool invalidReset = false, invalidSubmit = false, idle = false, destroyedBeforeIdle = false;
        int commandResets = 0, fenceResets = 0, submissions = 0, destroyedFences = 0;
        bool recordFault = false, submitFault = false, faultScopeUnwound = false;
        std::int32_t statusResult = 0, commandResetResult = 0, beginResult = 0, endResult = 0, fenceResetResult = 0, submitResult = 0;
    } gpu;
    struct FaultScope { ~FaultScope() { gpu.faultScopeUnwound = true; } };
    const void* gameSemaphore = reinterpret_cast<void*>(0x500);
    VkPresentInfoKHR present{}, adjusted{};
    const void* adjustedSemaphore = nullptr;

    void Check(bool ok, const char* description)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << description << '\n';
            std::exit(1);
        }
        std::cout << "PASS: " << description << '\n';
    }

    void Complete(std::size_t index)
    {
        gpu.pending[index] = false;
        gpu.signaled[index] = true;
    }

    void Setup()
    {
        gpu = {};
        g_renderer = {};
        g_renderer.ready = true;
        g_renderer.device = 0x1;
        g_renderer.commandBuffers = { 0x100, 0x101 };
        g_renderer.framebuffers = { 0x200, 0x201 };
        g_renderer.submissionFences = { 0x300, 0x301 };
        g_renderer.renderCompleteSemaphores = { 0x400, 0x401 };
        g_renderer.fns.getFenceStatus = [](void*, void* fence) -> std::int32_t {
            if (gpu.statusResult != VK_SUCCESS) return gpu.statusResult;
            return gpu.signaled[reinterpret_cast<uintptr_t>(fence) - 0x300] ? VK_SUCCESS : VK_NOT_READY;
        };
        g_renderer.fns.resetCommandBuffer = [](void* command, std::uint32_t) -> std::int32_t {
            ++gpu.commandResets;
            gpu.invalidReset |= gpu.pending[reinterpret_cast<uintptr_t>(command) - 0x100];
            return gpu.commandResetResult;
        };
        g_renderer.fns.beginCommandBuffer = [](void*, const VkCommandBufferBeginInfo*) -> std::int32_t { return gpu.beginResult; };
        g_renderer.fns.endCommandBuffer = [](void*) -> std::int32_t { return gpu.endResult; };
        g_renderer.fns.cmdBeginRenderPass = [](void*, const VkRenderPassBeginInfo*, std::uint32_t) {
            if (gpu.recordFault) { FaultScope scope; RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
        };
        g_renderer.fns.cmdEndRenderPass = [](void*) {};
        g_renderer.fns.resetFences = [](void*, std::uint32_t count, void* const* fences) -> std::int32_t {
            ++gpu.fenceResets;
            const auto index = reinterpret_cast<uintptr_t>(fences[0]) - 0x300;
            gpu.invalidReset |= count != 1 || gpu.pending[index];
            if (gpu.fenceResetResult == VK_SUCCESS) gpu.signaled[index] = false;
            return gpu.fenceResetResult;
        };
        g_renderer.fns.queueSubmit = [](void*, std::uint32_t count, const VkSubmitInfo* info, void* fence) -> std::int32_t {
            ++gpu.submissions;
            if (gpu.submitFault) { FaultScope scope; RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
            const auto index = reinterpret_cast<uintptr_t>(fence) - 0x300;
            if (index >= 2) { gpu.invalidSubmit = true; return TEST_ERROR; }
            gpu.invalidSubmit |= count != 1 || gpu.signaled[index] || gpu.pending[index] ||
                info->commandBufferCount != 1 || info->signalSemaphoreCount != 1 ||
                reinterpret_cast<uintptr_t>(info->pCommandBuffers[0]) != 0x100 + index ||
                reinterpret_cast<uintptr_t>(info->pSignalSemaphores[0]) != 0x400 + index ||
                info->waitSemaphoreCount != present.waitSemaphoreCount || info->pWaitSemaphores != present.pWaitSemaphores ||
                (info->waitSemaphoreCount != 0 && (info->pWaitDstStageMask == nullptr ||
                    info->pWaitDstStageMask[0] != VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT));
            if (gpu.submitResult == VK_SUCCESS) gpu.pending[index] = true;
            return gpu.submitResult;
        };
        g_renderer.fns.deviceWaitIdle = [](void*) -> std::int32_t {
            gpu.idle = true;
            Complete(0);
            Complete(1);
            return VK_SUCCESS;
        };
        g_renderer.fns.destroyFence = [](void*, void*, const void*) {
            ++gpu.destroyedFences;
            gpu.destroyedBeforeIdle |= !gpu.idle;
        };
        present = {};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &gameSemaphore;
        adjusted = {};
        adjusted.waitSemaphoreCount = 7; // Untouched unless an overlay was submitted.
        adjustedSemaphore = nullptr;
    }

    bool Submit(std::uint32_t index = 0)
    {
        return SubmitVulkanMinimapFrameLocked(reinterpret_cast<void*>(0x2), present, index, adjusted, &adjustedSemaphore);
    }

    void CheckVulkanFallbackScan()
    {
        constexpr std::size_t bytes = 4 * 1024 * 1024;
        auto memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(memory != nullptr, "allocate readable private memory for Vulkan fallback discovery");
        std::memset(memory, 0, bytes);
        const uintptr_t begin = reinterpret_cast<uintptr_t>(memory), end = begin + bytes;
        const uintptr_t table = end - VULKAN_TABLE_QUEUE_PRESENT_OFFSET - sizeof(uintptr_t);
        constexpr uintptr_t module = 0x12340000, proc = 0x56780000;
        for (std::size_t offset : {std::size_t(0x10), std::size_t(0x18), std::size_t(0x20), std::size_t(0x78),
            VULKAN_TABLE_CREATE_SWAPCHAIN_OFFSET, VULKAN_TABLE_GET_SWAPCHAIN_IMAGES_OFFSET, VULKAN_TABLE_QUEUE_PRESENT_OFFSET})
            *reinterpret_cast<uintptr_t*>(table + offset) = offset == 0x10 ? module : offset == 0x18 ? proc : reinterpret_cast<uintptr_t>(&Setup);

        g_vulkanDeviceTable = 0;
        g_vulkanScanCursor = 0;
        Check(!ScanExistingVulkanDeviceTable(module, proc, begin, end) &&
            g_vulkanScanCursor > begin && g_vulkanScanCursor < table,
            "large fallback scan yields before completion so other mods can process configuration");
        bool found = false;
        for (int slice = 0; slice < 1024 && !found; ++slice)
            found = ScanExistingVulkanDeviceTable(module, proc, begin, end);
        Check(found && g_vulkanDeviceTable.load() == table && g_vulkanScanCursor == 0,
            "resumed fallback discovers a table at the last valid address without skipping candidates");

        g_vulkanDeviceTable = 0;
        g_vulkanScanCursor = begin;
        Check(!ScanExistingVulkanDeviceTable(module, proc, begin, end), "start another partial fallback pass");
        const uintptr_t capturedByHook = begin + 0x1000;
        g_vulkanDeviceTable = capturedByHook;
        Check(ScanExistingVulkanDeviceTable(module, proc, begin, end) &&
            g_vulkanDeviceTable.load() == capturedByHook && g_vulkanScanCursor == 0,
            "normal Vulkan hook cancels pending discovery without replacing its captured table");

        g_vulkanDeviceTable = 0;
        std::memset(memory, 0, bytes);
        bool complete = false, noFalsePositive = true;
        for (int slice = 0; slice < 1024 && !complete; ++slice)
        {
            noFalsePositive = !ScanExistingVulkanDeviceTable(module, proc, begin, end) && noFalsePositive;
            complete = g_vulkanScanCursor == 0;
        }
        Check(noFalsePositive && g_vulkanDeviceTable.load() == 0, "empty scan does not invent a Vulkan table");
        Check(complete, "exhausted fallback pass resets its cursor for the delayed retry");
        DWORD oldProtect = 0;
        Check(VirtualProtect(memory, bytes, PAGE_NOACCESS, &oldProtect) != FALSE, "protect inaccessible scan fixture");
        Check(!ScanExistingVulkanDeviceTable(module, proc, begin, end) && g_vulkanScanCursor == 0,
            "fallback skips inaccessible pages without reading them");
        VirtualFree(memory, 0, MEM_RELEASE);
    }
}

int main()
{
    {
        MinimapMod rejected;
        // The loader writes the public SDK flag after Load returns, including
        // when the plugin rejected the current executable's client profile.
        rejected.loaded = true;
        rejected.active = true;
        rejected.Activate(nullptr);
        Check(!rejected.active && rejected.loaded,
            "loader-owned loaded flag cannot activate an uninitialized client profile");
        rejected.active = true;
        rejected.Activate(nullptr);
        Check(!rejected.active && !g_playerCameraTransformHook && !g_renderPresentFrameHook &&
            !g_vulkanDeviceTableInitHook,
            "repeated rejected activation cannot enter missing-hook activation path");
    }
    // Exercise the native reader with owned memory; no game process or guessed
    // native pointers are needed to verify bounds and exact-handle selection.
    {
        std::array<unsigned char, 0x348> frame{};
        std::array<unsigned char, 0x1B0> object{};
        uintptr_t objects[1] = { reinterpret_cast<uintptr_t>(object.data()) };
        uintptr_t images[2] = { 0xABC100, 0xABC200 };
        const uintptr_t handle = 0xABC000, device = 0xDEF000;
        const auto put = [](unsigned char* base, std::size_t offset, const auto& value) {
            std::memcpy(base + offset, &value, sizeof(value));
        };
        const uintptr_t objectList = reinterpret_cast<uintptr_t>(objects);
        const uintptr_t imageList = reinterpret_cast<uintptr_t>(images);
        put(frame.data(), 0x338, objectList);
        put(frame.data(), 0x340, std::uint64_t(1));
        put(object.data(), 0x38, handle);
        put(object.data(), 0x0C, std::uint8_t(44));
        put(object.data(), 0x1A8, std::uint32_t(1920));
        put(object.data(), 0x1AC, std::uint32_t(1080));
        put(object.data(), 0x04, std::uint32_t(640));
        put(object.data(), 0x08, std::uint32_t(480));
        put(object.data(), 0x48, imageList);
        put(object.data(), 0x50, std::uint64_t(2));
        SwapchainRuntimeInfo snapshot{};
        const char* reason = nullptr;
        const auto read = [&]() {
            return ReadRelease10SwapchainMetadata(reinterpret_cast<uintptr_t>(frame.data()),
                handle, device, snapshot, reason);
        };
        Check(read() && snapshot.handle == handle && snapshot.device == device &&
            snapshot.format == 44 && snapshot.width == 1920 && snapshot.height == 1080 &&
            snapshot.images == std::vector<uintptr_t>({ images[0], images[1] }) &&
            std::string(reason) == "validated_native_frame",
            "native exact-handle metadata uses actual extent instead of old render extent");
        put(object.data(), 0x38, uintptr_t(handle + 1));
        Check(!read() && snapshot.handle == 0 && std::string(reason) == "present_handle_not_in_frame",
            "native metadata refuses another swapchain and clears prior result");
        put(object.data(), 0x38, handle);
        put(frame.data(), 0x340, std::uint64_t(9));
        Check(!read() && std::string(reason) == "frame_list_bounds", "native frame rejects oversized object list");
        put(frame.data(), 0x340, std::uint64_t(0));
        Check(!read(), "native frame rejects empty object list");
        put(frame.data(), 0x340, std::uint64_t(1));
        put(object.data(), 0x1A8, std::uint32_t(0));
        Check(!read() && std::string(reason) == "metadata_bounds", "native metadata refuses zero actual extent");
        put(object.data(), 0x1A8, std::uint32_t(16385));
        Check(!read(), "native metadata refuses unreasonable extent");
        put(object.data(), 0x1A8, std::uint32_t(1920));
        put(object.data(), 0x50, std::uint64_t(17));
        Check(!read(), "native metadata refuses oversized image list");
        put(object.data(), 0x50, std::uint64_t(2));
        images[1] = images[0];
        Check(!read() && std::string(reason) == "invalid_image_handle", "native metadata refuses duplicate image handles");
        images[1] = 0;
        Check(!read(), "native metadata refuses null image handle");
        images[1] = 0xABC200;
        put(object.data(), 0x48, uintptr_t(1));
        Check(!read() && std::string(reason) == "image_list_unreadable", "native metadata refuses unreadable image array");
        put(object.data(), 0x48, imageList);
        Check(!ReadRelease10SwapchainMetadata(1, handle, device, snapshot, reason),
            "native metadata safely refuses unreadable frame");
        g_lastSwapchainState = reinterpret_cast<uintptr_t>(frame.data());
        g_lastVulkanDevice = device;
        Check(RecoverRelease10Swapchain(handle, snapshot) && snapshot.generation != 0,
            "verified native recovery publishes a generation");
        SwapchainRuntimeInfo cached{};
        Check(TryGetSwapchainSnapshot(handle, cached) && cached.width == 1920 && cached.format == 44 &&
            cached.images == snapshot.images, "native recovery cache preserves observed metadata");
        { std::lock_guard<std::mutex> lock(g_swapchainMutex); g_swapchains.clear(); }
        g_lastSwapchainState = 0;
        g_lastVulkanDevice = 0;
    }
    {
        wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
        Check(GetTempPathW(MAX_PATH, directory) != 0 && GetTempFileNameW(directory, L"mmd", 0, path) != 0,
            "create isolated diagnostic log fixture");
        InitializeDiagnosticLog(path);
        WriteDiagnosticLog("renderer-test-known-marker");
        std::ifstream input(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        Check(text.find("diagnostic session started") != std::string::npos &&
            text.find("renderer-test-known-marker") != std::string::npos &&
            text.find("local pid=") != std::string::npos && text.find(" tid=") != std::string::npos &&
            text.size() > 23 && text[4] == '-' && text[7] == '-' && text[13] == ':',
            "independent append log records session marker local time pid and tid");
        InitializeDiagnosticLog();
        Check(DeleteFileW(path) != FALSE, "diagnostic writer closes each append handle");
        EXCEPTION_RECORD record{};
        record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        record.ExceptionAddress = reinterpret_cast<void*>(0x12345678);
        EXCEPTION_POINTERS exception{ &record, nullptr };
        DiagnosticFault fault{};
        Check(CaptureDiagnosticFault(&exception, &fault) == EXCEPTION_EXECUTE_HANDLER &&
            fault.code == EXCEPTION_ACCESS_VIOLATION && fault.address == 0x12345678,
            "diagnostic SEH filter records access violation code and instruction address");
        record.ExceptionCode = EXCEPTION_IN_PAGE_ERROR;
        Check(CaptureDiagnosticFault(&exception, &fault) == EXCEPTION_EXECUTE_HANDLER &&
            fault.code == EXCEPTION_IN_PAGE_ERROR, "diagnostic SEH filter identifies in-page error");
        record.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
        Check(CaptureDiagnosticFault(&exception, &fault) == EXCEPTION_CONTINUE_SEARCH &&
            CaptureDiagnosticFault(nullptr, &fault) == EXCEPTION_CONTINUE_SEARCH,
            "diagnostic SEH filter does not swallow unrelated exceptions");
    }
    CheckVulkanFallbackScan();
    VULKAN_TABLE_CREATE_SWAPCHAIN_OFFSET = 0x580;
    VULKAN_TABLE_GET_SWAPCHAIN_IMAGES_OFFSET = 0x590;
    VULKAN_TABLE_ACQUIRE_NEXT_IMAGE_OFFSET = 0x598;
    VULKAN_TABLE_QUEUE_PRESENT_OFFSET = 0x5A0;
    CheckVulkanFallbackScan();
    VULKAN_TABLE_CREATE_SWAPCHAIN_OFFSET = 0x570;
    VULKAN_TABLE_GET_SWAPCHAIN_IMAGES_OFFSET = 0x580;
    VULKAN_TABLE_ACQUIRE_NEXT_IMAGE_OFFSET = 0x588;
    VULKAN_TABLE_QUEUE_PRESENT_OFFSET = 0x590;
    std::array<unsigned char, 0x600> incomplete{};
    g_vulkanDeviceTable = 0;
    CaptureVulkanDeviceTableInitHook(incomplete.data(), nullptr, nullptr, nullptr);
    Check(g_vulkanDeviceTable == 0 && g_pendingVulkanDeviceTable == reinterpret_cast<uintptr_t>(incomplete.data()),
        "uninitialized entry capture stays pending and cannot stop fallback discovery");
    Check(!TryInstallVulkanTableHooks(reinterpret_cast<uintptr_t>(incomplete.data())) &&
        g_patchedVulkanDeviceTable == 0, "invalid table does not patch slots");
    g_pendingVulkanDeviceTable = 0;
    static void* args[4]{};
    static bool initResult = false;
    g_release10VulkanInit = [](void* a, void* b, void* c, void* d) -> bool {
        args[0]=a; args[1]=b; args[2]=c; args[3]=d; return initResult;
    };
    Check(!CaptureRelease10VulkanInit(incomplete.data(), reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), reinterpret_cast<void*>(4)) &&
        args[0] == incomplete.data() && args[1] == reinterpret_cast<void*>(2) && args[2] == reinterpret_cast<void*>(3) && args[3] == reinterpret_cast<void*>(4),
        "post-init wrapper preserves four original arguments and failure result");
    initResult = true;
    Check(CaptureRelease10VulkanInit(incomplete.data(), nullptr, nullptr, nullptr) && g_patchedVulkanDeviceTable == 0,
        "post-init wrapper preserves success while refusing an invalid table");
    g_release10VulkanInit = nullptr;
    const wchar_t* hashFixture = L"build\\tests\\sha256-fixture.tmp";
    { std::ofstream fixture(hashFixture, std::ios::binary); fixture << "abc"; }
    Check(ClientExecutableMatchesSha256(hashFixture, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        "executable identity hashing matches independent SHA256 vector");
    Check(!ClientExecutableMatchesSha256(hashFixture, "4d6d43ea4a91e71c82d60b045dde932179c99e32e40657efe374688b2933589a"),
        "different executable is rejected");
    Check(!ClientExecutableMatchesSha256(L"missing-sha256-fixture", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
        "unreadable executable is rejected");
    DeleteFileW(hashFixture);
    // Same handle and extent do not establish that cached image views are valid.
    Setup();
    SwapchainRuntimeInfo snapshot{};
    snapshot.device = g_renderer.device;
    snapshot.handle = g_renderer.swapchain = 0x900;
    snapshot.width = g_renderer.width = 1920;
    snapshot.height = g_renderer.height = 1080;
    snapshot.format = g_renderer.format = 37;
    snapshot.images = g_renderer.images = { 0x910, 0x911 };
    snapshot.generation = g_renderer.swapchainGeneration = 1;
    Check(BuildVulkanMinimapRendererLocked(snapshot), "unchanged swapchain reuses the renderer");
    snapshot.images[0] = 0x912;
    Check(!RendererMatchesSwapchain(g_renderer, snapshot, 1920, 1080, 37), "changed image handles invalidate same-handle renderer");
    snapshot.images.clear();
    Check(RendererMatchesSwapchain(g_renderer, snapshot, 1920, 1080, 37), "missing image list alone does not rebuild each frame");
    ++snapshot.generation;
    Check(!RendererMatchesSwapchain(g_renderer, snapshot, 1920, 1080, 37), "known recreation invalidates renderer before image list arrives");
    VkSwapchainCreateInfoKHR create{};
    create.imageFormat = 37;
    create.imageExtent = { 1920, 1080 };
    RememberSwapchainCreate(reinterpret_cast<void*>(1), &create, reinterpret_cast<void*>(0x900), VK_SUCCESS);
    SwapchainRuntimeInfo first{}, second{};
    TryGetSwapchainSnapshot(0x900, first);
    RememberSwapchainCreate(reinterpret_cast<void*>(1), &create, reinterpret_cast<void*>(0x900), VK_SUCCESS);
    TryGetSwapchainSnapshot(0x900, second);
    Check(second.generation > first.generation && second.images.empty(), "same-handle creations have distinct generations");

    Setup();
    Check(Submit() && !gpu.invalidSubmit && !gpu.invalidReset && gpu.pending[0], "first frame submits with its completion fence");
    Check(adjusted.waitSemaphoreCount == 1 && adjusted.pWaitSemaphores == &adjustedSemaphore &&
        adjustedSemaphore == reinterpret_cast<void*>(0x400), "successful overlay updates the presentation wait");
    adjusted.waitSemaphoreCount = 7;
    adjustedSemaphore = nullptr;
    Check(!Submit() && gpu.commandResets == 1 && gpu.fenceResets == 1 && gpu.submissions == 1 &&
        !gpu.invalidReset && !g_renderer.submissionFailed, "busy GPU skips drawing without resetting or resubmitting pending work");
    Check(adjusted.waitSemaphoreCount == 7 && adjustedSemaphore == nullptr && present.pWaitSemaphores == &gameSemaphore,
        "skipped overlay leaves the game's presentation wait unchanged");
    Check(Submit(1) && gpu.pending[0] && gpu.pending[1] && !gpu.invalidSubmit, "swapchain images have independent completion tracking");
    Complete(0);
    Check(Submit() && gpu.submissions == 3 && !gpu.invalidReset, "completed image can be recorded and submitted again");
    for (std::uint32_t frame = 0; frame < 100000; ++frame)
    {
        const auto index = frame % 2;
        if (frame % 3 == 0) Complete(index);
        const bool expected = gpu.signaled[index];
        if (Submit(index) != expected || gpu.invalidReset || gpu.invalidSubmit)
            Check(false, "delayed completion stress sequence");
    }
    Check(true, "100000 submissions/skips with delayed GPU completion never reset pending work");
    DestroyVulkanMinimapRendererLocked();
    Check(gpu.destroyedFences == 2 && !gpu.destroyedBeforeIdle && !g_renderer.ready && g_renderer.submissionFences.empty(),
        "teardown waits before destroying fences and clearing state");

    Setup();
    Check(!Submit(2) && gpu.commandResets == 0 && gpu.submissions == 0, "invalid swapchain image is rejected");
    present.waitSemaphoreCount = 0;
    present.pWaitSemaphores = nullptr;
    Check(Submit() && !gpu.invalidSubmit, "presentation without game wait semaphores is supported");

    Setup();
    gpu.statusResult = TEST_ERROR;
    Check(!Submit() && g_renderer.submissionFailed && gpu.commandResets == 0 && gpu.submissions == 0,
        "fence query failure stops drawing before modifying resources");
    gpu.statusResult = VK_SUCCESS;
    Check(!Submit() && gpu.commandResets == 0, "drawing stays stopped after a synchronization failure");
    for (int stage = 0; stage < 3; ++stage)
    {
        Setup();
        if (stage == 0) gpu.commandResetResult = TEST_ERROR;
        if (stage == 1) gpu.beginResult = TEST_ERROR;
        if (stage == 2) gpu.endResult = TEST_ERROR;
        Check(!Submit() && g_renderer.submissionFailed && gpu.fenceResets == 0 && gpu.submissions == 0 &&
            adjusted.waitSemaphoreCount == 7, "recording failure preserves the signaled fence and original presentation");
    }
    Setup();
    gpu.fenceResetResult = TEST_ERROR;
    Check(!Submit() && g_renderer.submissionFailed && gpu.submissions == 0 && adjusted.waitSemaphoreCount == 7,
        "fence reset failure prevents queue submission");
    Setup();
    gpu.submitResult = TEST_ERROR;
    Check(!Submit() && g_renderer.submissionFailed && adjusted.waitSemaphoreCount == 7 && adjustedSemaphore == nullptr,
        "failed submission does not make presentation wait on an unsignaled overlay semaphore");
    gpu.submitResult = VK_SUCCESS;
    Check(!Submit() && gpu.submissions == 1 && gpu.commandResets == 1, "failed submission is not retried with uncertain state");
    Setup();
    gpu.recordFault = true;
    Check(!Submit() && g_renderer.submissionFailed && gpu.faultScopeUnwound &&
        gpu.fenceResets == 0 && gpu.submissions == 0 && adjusted.waitSemaphoreCount == 7,
        "recording access violation unwinds scopes and stops drawing without changing presentation");
    Setup();
    gpu.submitFault = true;
    Check(!Submit() && g_renderer.submissionFailed && gpu.faultScopeUnwound && adjusted.waitSemaphoreCount == 7,
        "submit access violation unwinds scopes and preserves original presentation waits");
    gpu.submitFault = false;
    Check(!Submit() && gpu.submissions == 1, "driver access violation is not retried on the same renderer");
    Setup();
    g_renderer.submissionFences = { 0x300, 0 };
    g_renderer.submissionFailed = true;
    DestroyVulkanMinimapRendererLocked();
    Check(gpu.destroyedFences == 1 && !gpu.destroyedBeforeIdle && !g_renderer.submissionFailed,
        "partial fence allocation cleans up valid handles and clears failure state");
    return 0;
}
