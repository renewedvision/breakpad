package worker

import (
	"sync"
)

type Pool struct {
	wg sync.WaitGroup
}

// StartWorkerPool will launch numWorkers goroutines all running workerFunc.
// When workerFunc exits, the goroutine will terminate.
func StartPool(numWorkers int, workerFunc func()) *Pool {
	p := new(Pool)
	for i := 0; i < numWorkers; i++ {
		p.wg.Add(1)
		go func() {
			workerFunc()
			p.wg.Done()
		}()
	}
	return p
}

// Wait for all the workers in the pool to complete the workerFunc.
func (p *Pool) Wait() {
	p.wg.Wait()
}
