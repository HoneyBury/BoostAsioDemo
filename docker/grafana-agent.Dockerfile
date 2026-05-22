# Boost Gateway -- Grafana Agent Sidecar Image
#
# Minimal Grafana Agent sidecar for metrics collection from
# gateway-server and backend services.
#
# Build:
#   docker build -f docker/grafana-agent.Dockerfile -t grafana-agent:latest .
#
# Run:
#   docker run -p 12345:12345 grafana-agent:latest
#
# This image embeds a minimal agent configuration that scrapes
# Prometheus metrics endpoints from the gateway and ships them
# to a remote Prometheus or Grafana Cloud endpoint.

FROM grafana/agent:v0.43.3

LABEL maintainer="BoostGateway Team"
LABEL description="Grafana Agent sidecar for Boost Gateway metrics collection"

# Default agent configuration -- override at runtime by mounting
# a custom config into /etc/agent/agent.yaml
COPY docker/agent-config.yaml /etc/agent/agent.yaml

EXPOSE 12345 9090

USER agent

ENTRYPOINT ["/bin/grafana-agent"]
CMD ["--config.file=/etc/agent/agent.yaml", "--server.http.listen-addr=0.0.0.0:12345"]
