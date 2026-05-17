using System;
using BoostGateway.Sdk;

static void Check(bool ok, string message)
{
    if (!ok)
    {
        Console.Error.WriteLine("FAIL: " + message);
        Environment.Exit(1);
    }
}

var host = args.Length > 0 ? args[0] : "127.0.0.1";
var port = args.Length > 1 ? ushort.Parse(args[1]) : (ushort)9201;

Console.WriteLine($"BoostGateway C# SDK full flow: {host}:{port}");
Console.WriteLine($"Native SDK: {SdkClient.Version}");

using var alice = new SdkClient();
using var bob = new SdkClient();

Check(alice.Connect(host, port), "alice connect");
Check(bob.Connect(host, port), "bob connect");

alice.StartHeartbeat(15);
bob.StartHeartbeat(15);

var loginAlice = alice.Login("alice_cs", "token:alice_cs");
Check(loginAlice.Ok != 0, "alice login: " + loginAlice.ErrorMessage);

var loginBob = bob.Login("bob_cs", "token:bob_cs");
Check(loginBob.Ok != 0, "bob login: " + loginBob.ErrorMessage);

var echo = alice.Echo("hello from csharp");
Check(echo.Ok != 0 && echo.Body.Contains("hello"), "echo");

var room = alice.CreateRoom("cs_room");
Check(room.Ok != 0, "create room: " + room.ErrorMessage);
Check(bob.JoinRoom("cs_room").Ok != 0, "bob join room");
Check(alice.SetReady(true).Ok != 0, "alice ready");
Check(bob.SetReady(true).Ok != 0, "bob ready");

var battle = alice.StartBattle("cs_room");
Check(battle.Ok != 0, "start battle: " + battle.ErrorMessage);
Check(alice.SendBattleInput("move:10,20").Ok != 0, "alice battle input");

alice.StopHeartbeat();
bob.StopHeartbeat();
alice.Disconnect();
bob.Disconnect();

Console.WriteLine("C# SDK full flow completed.");
