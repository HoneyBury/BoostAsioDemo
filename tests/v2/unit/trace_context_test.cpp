#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "v2/tracing/trace_context.h"

using v2::actor::MessageHeader;
using v2::actor::TraceId;
using v2::tracing::generate_span_id;
using v2::tracing::generate_trace_id;
using v2::tracing::Span;
using v2::tracing::SpanId;
using v2::tracing::TraceContext;

// ============================================================
// ID Generation
// ============================================================

TEST(V2TraceContextTest, GenerateTraceIdIsUnique) {
    auto id1 = generate_trace_id();
    auto id2 = generate_trace_id();
    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, 0);
    EXPECT_NE(id2, 0);
}

TEST(V2TraceContextTest, GenerateSpanIdIsUnique) {
    auto id1 = generate_span_id();
    auto id2 = generate_span_id();
    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, 0);
    EXPECT_NE(id2, 0);
}

// ============================================================
// Span Root
// ============================================================

TEST(V2TraceContextTest, SpanRootCreatesNewTraceId) {
    Span s = Span::root("op1");
    EXPECT_NE(s.trace_id, 0);
    EXPECT_NE(s.span_id, 0);
    EXPECT_EQ(s.parent_span_id, 0);
    EXPECT_EQ(s.operation_name, "op1");
    EXPECT_FALSE(s.finished);
}

// ============================================================
// Span Child
// ============================================================

TEST(V2TraceContextTest, SpanChildInheritsTraceId) {
    Span parent = Span::root("parent");
    Span child = Span::child(parent, "child");
    EXPECT_EQ(child.trace_id, parent.trace_id);
    EXPECT_NE(child.span_id, parent.span_id);
}

TEST(V2TraceContextTest, SpanChildHasParentSpanId) {
    Span parent = Span::root("parent");
    Span child = Span::child(parent, "child");
    EXPECT_NE(child.span_id, parent.span_id);
    EXPECT_EQ(child.parent_span_id, parent.span_id);
}

// ============================================================
// Span FromTrace
// ============================================================

TEST(V2TraceContextTest, SpanFromTrace) {
    TraceId existing_trace = 42;
    SpanId parent_span = 7;
    Span s = Span::from_trace(existing_trace, parent_span, "continued_op");
    EXPECT_EQ(s.trace_id, existing_trace);
    EXPECT_EQ(s.parent_span_id, parent_span);
    EXPECT_NE(s.span_id, 0);
    EXPECT_EQ(s.operation_name, "continued_op");
}

// ============================================================
// Span Duration
// ============================================================

TEST(V2TraceContextTest, SpanDurationIsPositive) {
    Span s = Span::root("measurable");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    s.finish();
    EXPECT_GT(s.duration_us(), 0);
    EXPECT_TRUE(s.finished);
}

// ============================================================
// Span Finish Idempotency
// ============================================================

TEST(V2TraceContextTest, SpanFinishIsIdempotent) {
    Span s = Span::root("idempotent");
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    s.finish();  // first finish — records end_time
    auto end_first = s.end_time;
    auto dur_first = s.duration_us();

    // second and third finish — should be no-ops
    s.finish();
    s.finish();

    EXPECT_EQ(s.end_time.time_since_epoch().count(),
              end_first.time_since_epoch().count());
    EXPECT_EQ(s.duration_us(), dur_first);
}

// ============================================================
// TraceContext from Header
// ============================================================

TEST(V2TraceContextTest, TraceContextFromHeader) {
    MessageHeader header;
    header.trace_id = 12345;
    header.request_id = 99;

    auto ctx = TraceContext::from_header(header);
    EXPECT_EQ(ctx.trace_id, 12345);
    EXPECT_EQ(ctx.current_span_id, 0);
}

// ============================================================
// TraceContext Apply to Header
// ============================================================

TEST(V2TraceContextTest, TraceContextApplyToHeader) {
    TraceContext ctx;
    ctx.trace_id = 67890;

    MessageHeader header;
    header.trace_id = 0;

    ctx.apply_to_header(header);
    EXPECT_EQ(header.trace_id, 67890);
}
