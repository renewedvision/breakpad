package breakpad

import (
	"debug/macho"
	"io"
	"log"
	"os"
	"path"
	"regexp"

	"upload_system_symbols/arch"
	"upload_system_symbols/worker"
)

var (
	// excludeRegexps match paths that should be excluded from dumping.
	excludeRegexps = []*regexp.Regexp{
		regexp.MustCompile(`/System/Library/Frameworks/Python\.framework/`),
		regexp.MustCompile(`/System/Library/Frameworks/Ruby\.framework/`),
		regexp.MustCompile(`_profile\.dylib$`),
		regexp.MustCompile(`_debug\.dylib$`),
		regexp.MustCompile(`\.a$`),
		regexp.MustCompile(`\.dat$`),
	}
)

// findQueue is an implementation detail of the DumpQueue that finds all the
// Mach-O files and their architectures.
type findQueue struct {
	*worker.Pool
	queue            chan string
	dq               *DumpQueue
	dumpArchitecture string
}

// findLibsInRoot looks in all the pathsToScan in all roots and manages the
// interaction between findQueue and DumpQueue.
func findLibsInRoots(roots []string, dq *DumpQueue) {
	fq := &findQueue{
		queue: make(chan string, 10),
		dq:    dq,
	}
	fq.Pool = worker.StartPool(12, fq.worker)
	for _, root := range roots {
		for _, p := range pathsToScan {
			fq.findLibsInPath(path.Join(root, p), true)
		}

		for _, p := range optionalPathsToScan {
			fq.findLibsInPath(path.Join(root, p), false)
		}
	}

	close(fq.queue)
	fq.Wait()
	dq.done()
}

// findLibsInPath recursively walks the directory tree, sending file paths to
// test for being Mach-O to the findQueue.
func (fq *findQueue) findLibsInPath(loc string, mustExist bool) {
	d, err := os.Open(loc)
	if err != nil {
		if !mustExist && os.IsNotExist(err) {
			return
		}
		log.Fatalf("Could not open %s: %v", loc, err)
	}
	defer d.Close()

	for {
		fis, err := d.Readdir(100)
		if err != nil && err != io.EOF {
			log.Fatalf("Error reading directory %s: %v", loc, err)
		}

		for _, fi := range fis {
			fp := path.Join(loc, fi.Name())
			if fi.IsDir() {
				fq.findLibsInPath(fp, true)
				continue
			} else if fi.Mode()&os.ModeSymlink != 0 {
				continue
			}

			// Test the exclude list in the worker to not slow down this main loop.

			fq.queue <- fp
		}

		if err == io.EOF {
			break
		}
	}
}

func (fq *findQueue) worker() {
	for fp := range fq.queue {
		excluded := false
		for _, re := range excludeRegexps {
			excluded = excluded || re.MatchString(fp)
		}
		if excluded {
			continue
		}

		f, err := os.Open(fp)
		if err != nil {
			log.Printf("%s: %v", fp, err)
			continue
		}

		fatFile, err := macho.NewFatFile(f)
		if err == nil {
			// The file is fat, so dump its architectures.
			for _, fatArch := range fatFile.Arches {
				fq.dumpMachOFile(fp, fatArch.File)
			}
			fatFile.Close()
		} else if err == macho.ErrNotFat {
			// The file isn't fat but may still be MachO.
			thinFile, err := macho.NewFile(f)
			if err != nil {
				log.Printf("%s: %v", fp, err)
				continue
			}
			fq.dumpMachOFile(fp, thinFile)
			thinFile.Close()
		} else {
			f.Close()
		}
	}
}

func (fq *findQueue) dumpMachOFile(fp string, image *macho.File) {
	if image.Type != arch.MachODylib &&
		image.Type != arch.MachOBundle &&
		image.Type != arch.MachODylinker {
		return
	}

	arch := arch.GetArchStringFromHeader(image.FileHeader)
	if arch == "" {
		// Don't know about this architecture type.
		return
	}

	if (fq.dumpArchitecture != "" && fq.dumpArchitecture == arch) || fq.dumpArchitecture == "" {
		fq.dq.DumpSymbols(fp, arch)
	}
}
