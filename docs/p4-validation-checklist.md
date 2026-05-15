# P4 Validation Checklist

Date: 2026-05-15

The final hardening pass for JWT, OTLP tracing, and gateway bridge behavior
should be validated with the following focused checks.

## JWT / auth

- `project_v2_unit_tests --gtest_filter="JwtValidatorTest.*"`
- `project_v2_integration_tests --gtest_filter="ServiceBusIntegrity.LoginBackendAcceptsRs256JwtAndValidatesToken"`

Expected:

- HS256 and RS256 validation both pass.
- RS256 login succeeds when issuer / audience match.
- invalid signatures are rejected by `token_validate`.

## Gateway bridge / readiness

- `project_v2_integration_tests --gtest_filter="V2BackendRoutingTest.SecurityPolicyAllowsPlaintextWhenGlobalTlsDisabled"`
- `project_v2_integration_tests --gtest_filter="V2DemoServerSmokeTest.ReadyJsonFailsWhenConfiguredBackendUnavailable"`

Expected:

- bridge routing still works when global TLS enforcement is disabled
- readiness reports the backend failure explicitly when a configured backend is
  unreachable

## OTLP tracing

- `project_v2_integration_tests --gtest_filter="V2BackendRoutingTest.OtelExporter*"`

Expected:

- failed and successful bridge routes both emit spans safely
- exporter preserves `route.<message_type>` operation names
- collector upload path receives `/v1/traces`

## Multi-process regression

- `project_v2_multi_process_tests --gtest_filter="MultiProcessFixture.*"`

Expected:

- login, room, ready, battle start, input, settlement, and post-finish error
  handling all complete over real OS processes
