# ns-3 runtime evidence

`GET /system/info` is the UI and export authority for platform/build/engine
metadata. The P0 baseline reports ns-3 `3.47`, C++23, and
`ns3::Simulator` as both the sole simulation-clock authority and the sole event-
scheduling authority. M1 / `Ns3KernelGateway` is only the Platform-side access
gateway to ns-3 scheduling; it is not a second scheduler. The launcher injects
the source revision and build configuration; missing values are published as
`unavailable`, never guessed from browser text.

The ns-3 ON quality gate must include, at minimum:

- `platform_ns3_kernel_smoke_test`;
- `platform_ns3_event_dispatcher_test`;
- `platform_ns3_signal_lifecycle_integration_test`;
- the acceptance HTTP/worker integration test.

Frontend SSE is observability-only. Browser connection loss, reconnect, replay
or rendering never advances ns-3 time and never changes Run lifecycle.
