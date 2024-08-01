package breakpad

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"path"
	"time"

	"upload_system_symbols/worker"
)

type UploadQueue struct {
	*worker.Pool
	queue         chan string
	apiKey        string
	servers       []string
	breakpadTools string
}

// StartUploadQueue creates a new worker pool and queue, to which paths to
// Breakpad symbol files may be sent for uploading.
func StartUploadQueue() *UploadQueue {
	uq := &UploadQueue{
		queue: make(chan string, 10),
	}
	uq.Pool = worker.StartPool(5, uq.worker)
	return uq
}

// Upload enqueues the contents of filepath to be uploaded.
func (uq *UploadQueue) Upload(filepath string) {
	uq.queue <- filepath
}

// Done tells the queue that no more files need to be uploaded. This must be
// called before WorkerPool.Wait.
func (uq *UploadQueue) Done() {
	close(uq.queue)
}

func (uq *UploadQueue) runSymUpload(symfile, server string) *exec.Cmd {
	symUpload := path.Join(uq.breakpadTools, "symupload")
	args := []string{symfile, server}
	if len(uq.apiKey) > 0 {
		args = append([]string{"-p", "sym-upload-v2", "-k", uq.apiKey}, args...)
	}
	return exec.Command(symUpload, args...)
}

func (uq *UploadQueue) worker() {
	for symfile := range uq.queue {
		for _, server := range uq.servers {
			for i := 0; i < 3; i++ { // Give each upload 3 attempts to succeed.
				cmd := uq.runSymUpload(symfile, server)
				if output, err := cmd.Output(); err == nil {
					// Success. No retry needed.
					fmt.Printf("Uploaded %s to %s\n", symfile, server)
					break
				} else if exitError, ok := err.(*exec.ExitError); ok && exitError.ExitCode() == 2 && uq.apiKey != "" {
					// Exit code 2 in protocol v2 means the file already exists on the server.
					// No point retrying.
					fmt.Printf("File %s already exists on %s\n", symfile, server)
					break
				} else {
					log.Printf("Error running symupload(%s, %s), attempt %d: %v: %s\n", symfile, server, i, err, output)
					time.Sleep(1 * time.Second)
				}
			}
		}
	}
}

// UploadFromDirectory handles the upload-only case and merely uploads all files in
// a directory.
func UploadFromDirectory(directory string, uq *UploadQueue) {
	d, err := os.Open(directory)
	if err != nil {
		log.Fatalf("Could not open directory to upload: %v", err)
	}
	defer d.Close()

	entries, err := d.Readdirnames(0)
	if err != nil {
		log.Fatalf("Could not read directory: %v", err)
	}

	for _, entry := range entries {
		uq.Upload(path.Join(directory, entry))
	}

	uq.Done()
}
