// SDK v4.1.0: Thin C# wrapper via C API (DllImport, zero deps).
using System;
using System.Runtime.InteropServices;

namespace BoostGateway.Sdk
{
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
    public struct LoginResult { public int Ok; public int ErrorCode;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)] public string UserId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)] public string DisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string ErrorMessage; }

    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
    public struct RoomResult { public int Ok; public int ErrorCode;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)] public string RoomId;
        public int MemberCount;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string ErrorMessage; }

    public class SdkClient : IDisposable
    {
        IntPtr _h;
        [DllImport("boost_gateway_sdk")] static extern IntPtr gsdk_create();
        [DllImport("boost_gateway_sdk")] static extern void gsdk_destroy(IntPtr h);
        [DllImport("boost_gateway_sdk")] static extern int gsdk_connect(IntPtr h, string host, ushort port, int ms);
        [DllImport("boost_gateway_sdk")] static extern void gsdk_disconnect(IntPtr h);
        [DllImport("boost_gateway_sdk")] static extern LoginResult gsdk_login(IntPtr h, string uid, string token, int ms);
        [DllImport("boost_gateway_sdk")] static extern RoomResult gsdk_create_room(IntPtr h, string rid, int ms);
        [DllImport("boost_gateway_sdk")] static extern RoomResult gsdk_join_room(IntPtr h, string rid, int ms);
        [DllImport("boost_gateway_sdk")] static extern RoomResult gsdk_leave_room(IntPtr h, string rid, int ms);
        [DllImport("boost_gateway_sdk")] static extern RoomResult gsdk_set_ready(IntPtr h, int ready, int ms);

        public SdkClient() { _h = gsdk_create(); }
        public void Dispose() { if (_h != IntPtr.Zero) { gsdk_destroy(_h); _h = IntPtr.Zero; } }
        public bool Connect(string h="127.0.0.1", ushort p=9201, int ms=5000) => gsdk_connect(_h, h, p, ms) != 0;
        public void Disconnect() => gsdk_disconnect(_h);
        public LoginResult Login(string u, string t, int ms=5000) => gsdk_login(_h, u, t, ms);
        public RoomResult CreateRoom(string r, int ms=5000) => gsdk_create_room(_h, r, ms);
        public RoomResult JoinRoom(string r, int ms=5000) => gsdk_join_room(_h, r, ms);
        public RoomResult LeaveRoom(string r, int ms=5000) => gsdk_leave_room(_h, r, ms);
        public RoomResult SetReady(bool r=true, int ms=5000) => gsdk_set_ready(_h, r?1:0, ms);
    }
}
