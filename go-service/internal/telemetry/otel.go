package telemetry

import (
	"context"
	"time"

	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/exporters/otlp/otlpmetric/otlpmetricgrpc"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc"
	"go.opentelemetry.io/otel/metric"
	sdkmetric "go.opentelemetry.io/otel/sdk/metric"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	semconv "go.opentelemetry.io/otel/semconv/v1.26.0"
)

type Provider struct {
	tracerProvider *sdktrace.TracerProvider
	meterProvider  *sdkmetric.MeterProvider

	JobsProcessed metric.Int64Counter
	ExecDuration  metric.Float64Histogram
	CyclesTotal   metric.Int64Counter
	ActiveWorkers metric.Int64UpDownCounter
}

func Init(ctx context.Context, endpoint string) (*Provider, error) {
	res, err := resource.New(ctx,
		resource.WithAttributes(semconv.ServiceName("stm32-sim-service")),
	)
	if err != nil {
		return nil, err
	}

	traceExp, err := otlptracegrpc.New(ctx,
		otlptracegrpc.WithEndpoint(endpoint),
		otlptracegrpc.WithInsecure(),
	)
	if err != nil {
		return nil, err
	}
	tp := sdktrace.NewTracerProvider(
		sdktrace.WithBatcher(traceExp),
		sdktrace.WithResource(res),
	)
	otel.SetTracerProvider(tp)

	metricExp, err := otlpmetricgrpc.New(ctx,
		otlpmetricgrpc.WithEndpoint(endpoint),
		otlpmetricgrpc.WithInsecure(),
	)
	if err != nil {
		return nil, err
	}
	mp := sdkmetric.NewMeterProvider(
		sdkmetric.WithReader(sdkmetric.NewPeriodicReader(metricExp,
			sdkmetric.WithInterval(15*time.Second))),
		sdkmetric.WithResource(res),
	)
	otel.SetMeterProvider(mp)

	meter := mp.Meter("stm32sim")

	p := &Provider{tracerProvider: tp, meterProvider: mp}

	p.JobsProcessed, err = meter.Int64Counter("sim.jobs.processed",
		metric.WithDescription("Total simulation jobs processed"))
	if err != nil {
		return nil, err
	}
	p.ExecDuration, err = meter.Float64Histogram("sim.execution.duration",
		metric.WithDescription("Simulation wall-clock duration"),
		metric.WithUnit("ms"))
	if err != nil {
		return nil, err
	}
	p.CyclesTotal, err = meter.Int64Counter("sim.cycles.total",
		metric.WithDescription("Total simulator cycles executed"))
	if err != nil {
		return nil, err
	}
	p.ActiveWorkers, err = meter.Int64UpDownCounter("sim.workers.active",
		metric.WithDescription("Number of workers currently executing a job"))
	if err != nil {
		return nil, err
	}

	return p, nil
}

func (p *Provider) Shutdown(ctx context.Context) {
	if p.tracerProvider != nil {
		_ = p.tracerProvider.Shutdown(ctx)
	}
	if p.meterProvider != nil {
		_ = p.meterProvider.Shutdown(ctx)
	}
}

// InitNoOp configures the provider with no-op (discarding) instruments.
// Used when OTEL_EXPORTER_OTLP_ENDPOINT is not set.
func InitNoOp(p *Provider) error {
	mp := sdkmetric.NewMeterProvider()
	otel.SetMeterProvider(mp)
	p.meterProvider = mp

	meter := mp.Meter("stm32sim")
	var err error
	p.JobsProcessed, err = meter.Int64Counter("sim.jobs.processed")
	if err != nil {
		return err
	}
	p.ExecDuration, err = meter.Float64Histogram("sim.execution.duration")
	if err != nil {
		return err
	}
	p.CyclesTotal, err = meter.Int64Counter("sim.cycles.total")
	if err != nil {
		return err
	}
	p.ActiveWorkers, err = meter.Int64UpDownCounter("sim.workers.active")
	return err
}
