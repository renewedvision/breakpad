package breakpad

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"path"
	"strings"

	"upload_system_symbols/worker"
)

var (
	// pathsToScan are the subpaths in the systemRoot that should be scanned for shared libraries.
	pathsToScan = []string{
		"/System/Library/Frameworks",
		"/System/Library/PrivateFrameworks",
		"/usr/lib",
	}

	// optionalPathsToScan is just like pathsToScan, but the paths are permitted to be absent.
	optionalPathsToScan = []string{
		// Gone in 10.15.
		"/Library/QuickTime",
		// Not present in dumped dyld_shared_caches
		"/System/Library/Components",
	}
)

// manglePath reduces an absolute filesystem path to a string suitable as the
// base for a file name which encodes some of the original path. The result
// concatenates the leading initial from each path component except the last to
// the last path component; for example /System/Library/Frameworks/AppKit
// becomes SLFAppKit.
// Assumes ASCII.
func manglePath(path string) string {
	components := strings.Split(path, "/")
	n := len(components)
	builder := strings.Builder{}
	for i, component := range components {
		if len(component) == 0 {
			continue
		}
		if i < n-1 {
			builder.WriteString(component[:1])
		} else {
			builder.WriteString(component)
		}
	}
	return builder.String()
}

type DumpQueue struct {
	*worker.Pool
	dumpPath      string
	queue         chan dumpRequest
	uq            *UploadQueue
	breakpadTools string
}

type dumpRequest struct {
	path string
	arch string
}

// StartDumpQueue creates a new worker pool to find all the Mach-O libraries in
// root and dump their symbols to dumpPath. If an UploadQueue is passed, the
// path to the symbol file will be enqueued there, too.
func StartDumpQueue(roots []string, dumpPath string, uq *UploadQueue) *DumpQueue {
	dq := &DumpQueue{
		dumpPath: dumpPath,
		queue:    make(chan dumpRequest),
		uq:       uq,
	}
	dq.Pool = worker.StartPool(12, dq.worker)

	findLibsInRoots(roots, dq)

	return dq
}

// DumpSymbols enqueues the filepath to have its symbols dumped in the specified
// architecture.
func (dq *DumpQueue) DumpSymbols(filepath string, arch string) {
	dq.queue <- dumpRequest{
		path: filepath,
		arch: arch,
	}
}

func (dq *DumpQueue) Wait() {
	dq.Pool.Wait()
	if dq.uq != nil {
		dq.uq.Done()
	}
}

func (dq *DumpQueue) done() {
	close(dq.queue)
}

func (dq *DumpQueue) worker() {
	dumpSyms := path.Join(dq.breakpadTools, "dump_syms")

	for req := range dq.queue {
		filebase := path.Join(dq.dumpPath, manglePath(req.path))
		symfile := fmt.Sprintf("%s_%s.sym", filebase, req.arch)
		f, err := os.Create(symfile)
		if err != nil {
			log.Fatalf("Error creating symbol file: %v", err)
		}

		cmd := exec.Command(dumpSyms, "-a", req.arch, req.path)
		cmd.Stdout = f
		err = cmd.Run()
		f.Close()

		if err != nil {
			os.Remove(symfile)
			log.Printf("Error running dump_syms(%s, %s): %v\n", req.arch, req.path, err)
		} else if dq.uq != nil {
			dq.uq.Upload(symfile)
		}
	}
}
