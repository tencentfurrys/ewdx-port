// hmm_spy.zig — PC-side draw spy for Echidna Wars DX.
// Drop-in replacement for hmm.dll: forwards EVERY export to the real DLL
// (renamed hmm_real.dll) while logging the DG* draw stream in the same
// vocabulary as the Android port journals, so PC ground truth and port logs
// diff line-by-line.
//
// Build: python -m ziglang build-lib -target x86-windows-gnu -dynamic
//        -O ReleaseFast -femit-bin=hmm.dll hmm_spy.zig
//
// Install: copy into the game folder, rename the original hmm.dll to
// hmm_real.dll. Output: hmm_spy.log next to the exe. The full stream goes to
// a 4 MB ring buffer; the ring FLUSHES to the log whenever DGGCOPY 5 (the POV
// composite) fires, so every squeeze moment yields a complete trace.

const std = @import("std");
const win = std.os.windows;

const DLL_PROCESS_ATTACH = 1;
const DLL_PROCESS_DETACH = 0;
const k32 = win.kernel32;

// ---------------------------------------------------------------- logging
var log_buf: [4 * 1024 * 1024]u8 = undefined;
var log_len: usize = 0;
var log_cs: win.CRITICAL_SECTION = undefined;
var log_file: ?std.fs.File = null;
var episode: u32 = 0;
var call_tick: u64 = 0;

fn ringAppend(bytes: []const u8) void {
    if (log_len + bytes.len > log_buf.len) return; // ring full: drop until flush
    @memcpy(log_buf[log_len..][0..bytes.len], bytes);
    log_len += bytes.len;
}

fn logf(comptime fmt: []const u8, args: anytype) void {
    var sb: [512]u8 = undefined;
    const s = std.fmt.bufPrint(&sb, fmt, args) catch return;
    _ = k32.EnterCriticalSection(&log_cs);
    defer _ = k32.LeaveCriticalSection(&log_cs);
    call_tick += 1;
    ringAppend(s);
}

fn flushRing() void {
    if (log_file == null) return;
    _ = k32.EnterCriticalSection(&log_cs);
    defer _ = k32.LeaveCriticalSection(&log_cs);
    if (log_len == 0) return;
    log_file.?.writeAll(log_buf[0..log_len]) catch {};
    log_len = 0;
}

fn openLog() void {
    log_file = std.fs.cwd().createFile("hmm_spy.log", .{ .truncate = false }) catch null;
    if (log_file) |f| {
        f.seekFromEnd(0) catch {};
        f.writeAll("=== hmm_spy episode stream v1 ===\r\n") catch {};
    }
}

// ---------------------------------------------------------------- real dll
var real_hmod: win.HMODULE = null;

fn realProc(comptime name: [:0]const u8) win.FARPROC {
    return k32.GetProcAddress(real_hmod, name.ptr);
}

fn fwd4(comptime name: [:0]const u8) *const fn (i32, i32, i32, i32) callconv(win.WINAPI) i32 {
    return @ptrCast(realProc(name));
}
fn fwd0(comptime name: [:0]const u8) *const fn () callconv(win.WINAPI) i32 {
    return @ptrCast(realProc(name));
}

// ---------------------------------------------------------------- logged DG*
fn DGCOLOR(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGCOLOR %d %d %d %d\r\n", .{ a0, a1, a2, a3 });
    return fwd4("_DGCOLOR@16")(a0, a1, a2, a3);
}

fn DGGSEL(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGGSEL %d\r\n", .{a0});
    return fwd4("_DGGSEL@16")(a0, a1, a2, a3);
}

fn DGCLEAR() callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGCLEAR\r\n", .{});
    return fwd0("_DGCLEAR@16")();
}

fn DGRECT(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGRECT %d %d %d %d\r\n", .{ a0, a1, a2, a3 });
    return fwd4("_DGRECT@16")(a0, a1, a2, a3);
}

fn DGPOS(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGPOS %d %d\r\n", .{ a0, a1 });
    return fwd4("_DGPOS@16")(a0, a1, a2, a3);
}

fn DGSCALEANDANGLE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGSCALEANDANGLE %d %d %d sc=({d:.2},{d:.2})\r\n", .{ a0, a1, a2, @as(f32, @floatFromInt(a0)) / 256.0, @as(f32, @floatFromInt(a1)) / 256.0 });
    return fwd4("_DGSCALEANDANGLE@16")(a0, a1, a2, a3);
}

fn DGBLENDMODE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGBLENDMODE %d\r\n", .{a0});
    return fwd4("_DGBLENDMODE@16")(a0, a1, a2, a3);
}

fn DGGCOPY(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGGCOPY id=%d flags=0x{x}\r\n", .{ a0, a1 });
    if (a0 == 5) {
        episode += 1;
        logf("[pc-dump] FLUSH ep={d} t={d}\r\n", .{ episode, call_tick });
        flushRing();
    }
    return fwd4("_DGGCOPY@16")(a0, a1, a2, a3);
}

fn DGTEXTURE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGTEXTURE %d\r\n", .{a0});
    return fwd4("_DGTEXTURE@16")(a0, a1, a2, a3);
}

fn DGADDPRIMITIVE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-prim] DGADDPRIMITIVE flags=0x{x}\r\n", .{a0});
    return fwd4("_DGADDPRIMITIVE@16")(a0, a1, a2, a3);
}

fn DGDRAWPRIMITIVE() callconv(win.WINAPI) i32 {
    logf("[pc-prim] DGDRAWPRIMITIVE\r\n", .{});
    return fwd0("_DGDRAWPRIMITIVE@16")();
}

fn DGCREATEPRIMITIVE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-prim] DGCREATEPRIMITIVE %d\r\n", .{a0});
    return fwd4("_DGCREATEPRIMITIVE@16")(a0, a1, a2, a3);
}

fn DGBUFFER(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc] DGBUFFER id=%d %dx%d\r\n", .{ a0, a1, a2 });
    return fwd4("_DGBUFFER@16")(a0, a1, a2, a3);
}

fn DGREDRAW() callconv(win.WINAPI) i32 {
    logf("[pc] DGREDRAW\r\n", .{});
    return fwd0("_DGREDRAW@16")();
}

fn DGLINE(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
    logf("[pc-maw] DGLINE %d %d -> %d %d\r\n", .{ a0, a1, a2, a3 });
    return fwd4("_DGLINE@16")(a0, a1, a2, a3);
}

// ------------------------------------------------------- plain forwarders
const plain_names = [_][:0]const u8{
    "_CHECKPLAY@16",      "_DDADDGCOPY@16",   "_DDADDGCOPYALL@16", "_DDBGCOPY@16",
    "_DDBLENDGCOPY@16",   "_DDBOXF@16",       "_DDBUFFER@16",      "_DDCOLOR@16",
    "_DDDRAWTEXT@16",     "_DDEND@16",        "_DDGCOPY2@16",      "_DDGCOPY@16",
    "_DDGSEL@16",         "_DDGZOOM@16",      "_DDINIT@16",        "_DDLOADFNAME@16",
    "_DDLOADMEMORY@16",   "_DDPAINTGCOPY@16", "_DDPOS@16",         "_DDPRINT@16",
    "_DDREDRAW@16",       "_DDREVERSE@16",    "_DDROTATEGCOPY@16", "_DDSCREEN@16",
    "_DDSETRECT@16",      "_DDSETRENEWALTIMING@16", "_DDSUBGCOPY@16", "_DGBMPSAVE@16",
    "_DGDRAWTEXT@16",     "_DGEND@16",        "_DGFONT@16",        "_DGINIT@16",
    "_DGLOADFNAME@16",    "_DGLOADMEMORY@16", "_DGRENEWALTIMING@16", "_DGSCREEN@16",
    "_DIEND@16",          "_DIGETJOYNUM@16",  "_DIGETJOYSTATE@16", "_DIGETKEEPJOYSTATE@16",
    "_DIGETKEEPKEYSTATE@16", "_DIGETKEYSTATE@16", "_DIGETMOMENTJOYSTATE@16",
    "_DIGETMOMENTKEYSTATE@16", "_DIGETPASTJOYSTATE@16", "_DIGETPASTKEYSTATE@16",
    "_DIINIT@16",         "_DIPLAYEFFECT@16", "_DISTICK@16",       "_DISTOPEFFECT@16",
    "_DMEND@16",          "_DMINIT@16",       "_DMLOADFNAME@16",   "_DMLOADMEMORY@16",
    "_DMPLAY@16",         "_DMSTOP@16",       "_DSDUPLICATE@16",   "_DSEND@16",
    "_DSGETMASTERVOLUME@16", "_DSGETVOLUME@16", "_DSHCHECKPLAY@16", "_DSHEND@16",
    "_DSHGETORIGINALVIDEOSIZE@16", "_DSHGETPLAYPOSITION@16", "_DSHINIT@16",
    "_DSHLOADFNAME@16",  "_DSHPAUSE@16",     "_DSHPLAY@16",       "_DSHSETFULLSCREEN@16",
    "_DSHSETMOVIETODGBUFFER@16", "_DSHSETRATE@16", "_DSHSETSEEK@16",
    "_DSHSETVIDEOPARAM@16", "_DSHSETVIDEOVISIBLE@16", "_DSHSETVOLUME@16", "_DSHSTOP@16",
    "_DSINIT@16",        "_DSLOADFNAME2@16", "_DSLOADFNAME@16",   "_DSLOADMEMORY@16",
    "_DSOGGLOADFNAME@16", "_DSOGGLOADFNAMETHREAD@16", "_DSOGGLOADINGCHECK@16",
    "_DSPLAY@16",        "_DSRELEASE@16",    "_DSSETMASTERFORMAT@16",
    "_DSSETMASTERVOLUME@16", "_DSSETVOLUME@16", "_DSSTOP@16",
    "_HMMBITCHECK@16",    "_HMMBITOFF@16",    "_HMMBITON@16",      "_HMMEND@16",
    "_HMMGETFPS@16",      "_HMMGETSIN@16",    "_HMMHITCHECK@16",   "_HMMHITCHECKSETINDEX@16",
    "_HMMINIT@16",       "_HMMSLEEP@16",
};

fn genForwards(comptime names: []const [:0]const u8) void {
    inline for (names) |nm| {
        const S = struct {
            fn proc(a0: i32, a1: i32, a2: i32, a3: i32) callconv(win.WINAPI) i32 {
                return fwd4(nm)(a0, a1, a2, a3);
            }
        };
        @export(&S.proc, .{ .name = nm });
    }
}

// ---------------------------------------------------------------- DllMain
fn dllMain(hinst: win.HINSTANCE, reason: u32, reserved: ?*anyopaque) callconv(win.WINAPI) i32 {
    _ = hinst;
    _ = reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        k32.InitializeCriticalSection(&log_cs);
        openLog();
        real_hmod = k32.LoadLibraryA("hmm_real.dll");
        if (real_hmod == null) {
            _ = k32.MessageBoxA(null, "hmm_spy: cannot load hmm_real.dll - rename the original hmm.dll to hmm_real.dll", "hmm_spy", 0);
            return 0;
        }
        logf("[pc] hmm_spy attached real=0x{x}\r\n", .{@intFromPtr(real_hmod)});
    } else if (reason == DLL_PROCESS_DETACH) {
        flushRing();
        if (log_file) |f| f.close();
    }
    return 1;
}

comptime {
    @export(&DGCOLOR, .{ .name = "_DGCOLOR@16" });
    @export(&DGGSEL, .{ .name = "_DGGSEL@16" });
    @export(&DGCLEAR, .{ .name = "_DGCLEAR@16" });
    @export(&DGRECT, .{ .name = "_DGRECT@16" });
    @export(&DGPOS, .{ .name = "_DGPOS@16" });
    @export(&DGSCALEANDANGLE, .{ .name = "_DGSCALEANDANGLE@16" });
    @export(&DGBLENDMODE, .{ .name = "_DGBLENDMODE@16" });
    @export(&DGGCOPY, .{ .name = "_DGGCOPY@16" });
    @export(&DGTEXTURE, .{ .name = "_DGTEXTURE@16" });
    @export(&DGADDPRIMITIVE, .{ .name = "_DGADDPRIMITIVE@16" });
    @export(&DGDRAWPRIMITIVE, .{ .name = "_DGDRAWPRIMITIVE@16" });
    @export(&DGCREATEPRIMITIVE, .{ .name = "_DGCREATEPRIMITIVE@16" });
    @export(&DGBUFFER, .{ .name = "_DGBUFFER@16" });
    @export(&DGREDRAW, .{ .name = "_DGREDRAW@16" });
    @export(&DGLINE, .{ .name = "_DGLINE@16" });
    genForwards(&plain_names);
    @export(&dllMain, .{ .name = "_DllMain@12" });
}
