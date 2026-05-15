$ErrorActionPreference = "Stop"

param(
    [string]$BuildDir = "D:\Program\boost-github\BoostAsioDemo\build\windows-msvc-debug"
)

function Invoke-Step {
    param(
        [string]$Name,
        [string]$Command
    )

    Write-Host "==> $Name"
    & powershell -NoProfile -Command $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed: $Name"
    }
}

$root = Split-Path -Parent $PSScriptRoot
$operatorDir = Join-Path $root "operator\boostgateway-operator"

Invoke-Step "RemoteActor and Raft unit tests" @"
Set-Location '$BuildDir'
if (Test-Path '.\tests\v2\Debug\project_v2_unit_tests.exe') {
  .\tests\v2\Debug\project_v2_unit_tests.exe --gtest_filter=`"RemoteActorTest.*:RaftTest.*:RaftClusterTest.*:ProtoSchemaTest.*`"
} else {
  throw 'project_v2_unit_tests.exe not found'
}
"@

Invoke-Step "Backend routing integration tests" @"
Set-Location '$BuildDir'
if (Test-Path '.\tests\v2\Debug\project_v2_integration_tests.exe') {
  .\tests\v2\Debug\project_v2_integration_tests.exe --gtest_filter=`"V2BackendRoutingTest.LeaderboardReplicatesCommittedScoresAcrossRaftFollowers:V2BackendRoutingTest.MatchmakingReplicatesQueuedPlayersAndMatchesAcrossFollowers:V2BackendRoutingTest.MatchmakingReplicatesExpiredQueuePurgeAcrossFollowers:V2BackendRoutingTest.LeaderboardRestoresCommittedScoresAfterRestart:V2BackendRoutingTest.MatchmakingRestoresCommittedMatchAfterRestart:V2BackendRoutingTest.LeaderboardFollowerCatchesUpAfterLeaderRestart:ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughMatchBackend:ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughLeaderboardBackend`"
} else {
  throw 'project_v2_integration_tests.exe not found'
}
"@

Invoke-Step "Operator fake-client tests" @"
Set-Location '$operatorDir'
$env:GOCACHE='C:\Users\Administrator\.codex\memories\go-build-cache'
$env:GOMODCACHE='C:\Users\Administrator\.codex\memories\go-mod-cache'
go test ./...
"@

Write-Host "Validation completed."
