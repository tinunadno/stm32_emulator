package worker

import (
	"context"
	"fmt"
	"sync"

	"stm32sim-service/internal/queue"
	"stm32sim-service/internal/simulator"
	"stm32sim-service/internal/telemetry"
)

type Pool struct {
	workers []*Worker
	wg      sync.WaitGroup
	cancel  context.CancelFunc
}

func NewPool(n int, q *queue.Queue, simCfg simulator.Config, tel *telemetry.Provider) *Pool {
	p := &Pool{}
	for i := 0; i < n; i++ {
		id := fmt.Sprintf("worker-%d", i+1)
		p.workers = append(p.workers, New(id, q, simCfg, tel))
	}
	return p
}

func (p *Pool) Start(ctx context.Context) {
	ctx, p.cancel = context.WithCancel(ctx)
	for _, w := range p.workers {
		p.wg.Add(1)
		go func(w *Worker) {
			defer p.wg.Done()
			w.Run(ctx)
		}(w)
	}
}

func (p *Pool) Shutdown() {
	p.cancel()
	p.wg.Wait()
}
