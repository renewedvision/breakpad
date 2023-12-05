package main

// #cgo LDFLAGS: -lParallelCompression
// #include <stdio.h>
// #include <stdint.h>
// #include <stdlib.h>
//
// typedef struct
// {
//   int64_t unknown1;
//   int64_t unknown2;
//   char *input;
//   char *output;
//   char *patch;
//   uint32_t not_cryptex_cache;
//   uint32_t threads;
//   uint32_t verbose;
// } RawImage;
//
// extern int32_t RawImagePatch(RawImage *);
import "C"

import (
	"archive/zip"
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"unsafe"
)

func pathExists(path string) bool {
	_, err := os.Stat(path)
	return !errors.Is(err, fs.ErrNotExist)
}

func extractFileToPath(f *zip.File, path string) error {
	w, err := os.Create(path)
	if err != nil {
		return err
	}
	defer w.Close()
	return extractFile(f, w)
}

func extractFile(f *zip.File, w io.Writer) error {
	reader, err := f.Open()
	if err != nil {
		return err
	}

	if _, err := io.Copy(w, reader); err != nil {
		return err
	}
	return nil
}

func copyFile(src, dst string) error {
	w, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer w.Close()

	reader, err := os.Open(src)
	if err != nil {
		return err
	}
	if _, err := io.Copy(w, reader); err != nil {
		return err
	}
	return nil
}

func mountDMG(dmgPath string, mountpoint string) error {
	cmd := exec.Command("hdiutil", "attach", dmgPath, "-mountpoint", mountpoint, "-quiet", "-nobrowse", "-readonly")
	return cmd.Run()
}

// XXX: how does returning an error work with defer? should this panic?
func unmountDMG(mountpoint string) error {
	return exec.Command("hdiutil", "detach", mountpoint).Run()
}

// Extractor is an interface for extracting a dyld shared cache from a packaged system.
type Extractor interface {
	// Extract parses `inputFile` and extracts any found dyld shared cache into
	// `destination`. Returns nil on success or error on failure.
	Extract(inputFile string, destination string, verbose bool) error
}

// Extractor
type InstallAssistantExtractor struct {
	verbose    bool
	scratchDir string
}

func (e *InstallAssistantExtractor) vlog(format string, args ...interface{}) {
	if !e.verbose {
		return
	}
	fmt.Printf(format, args...)
}

func (e *InstallAssistantExtractor) payloadHasSharedCache(payloadPath string) bool {
	out, err := exec.Command("yaa", "list", "-i", payloadPath).Output()
	return err == nil && bytes.Contains(out, []byte("/dyld_shared_cache"))
}

func (e *InstallAssistantExtractor) unarchiveFilesMatching(r *zip.ReadCloser, destination string, glob string) error {
	if err := os.MkdirAll(destination, 0755); err != nil {
		return err
	}
	for _, file := range r.File {
		if file.FileInfo().IsDir() {
			continue
		}
		if ok, err := path.Match(glob, file.Name); !ok || err != nil {
			continue
		}
		_, filename := path.Split(file.Name)
		writePath := path.Join(destination, filename)
		if err := extractFileToPath(file, writePath); err != nil {
			return err
		}
	}
	return nil
}

func (e *InstallAssistantExtractor) extractPayload(payloadPath string, destination string) error {
	return exec.Command("yaa", "extract", "-i", payloadPath, "-d", destination).Run()
}

func (e *InstallAssistantExtractor) expandInstaller(installerPath string, destination string) error {
	return exec.Command("pkgutil", "--expand-full", installerPath, destination).Run()
}

func (e *InstallAssistantExtractor) copySharedCaches(from, to string) error {
	dyldPath := path.Join(from, "System/Library/dyld")
	if !pathExists(dyldPath) {
		return fmt.Errorf("couldn't find System/Library/dyld in %s", dyldPath)
	}
	cacheFiles, err := os.ReadDir(dyldPath)
	if err != nil {
		return err
	}
	for _, cacheFile := range cacheFiles {
		name := cacheFile.Name()
		src := path.Join(dyldPath, name)
		dst := path.Join(to, name)
		e.vlog("Copying %v to %v\n", src, dst)
		if err := copyFile(src, dst); err != nil {
			return fmt.Errorf("couldn't copy %s to %s: %v", src, dst, err)
		}
	}
	return nil
}

func (e *InstallAssistantExtractor) extractCachesFromPayloads(payloadsPath string, destination string) error {
	scratchDir, err := os.MkdirTemp(e.scratchDir, "payload")
	if err != nil {
		return err
	}
	defer os.RemoveAll(scratchDir)
	files, err := os.ReadDir(payloadsPath)
	if err != nil {
		return err
	}
	for _, f := range files {
		payload := path.Join(payloadsPath, f.Name())
		if e.payloadHasSharedCache(payload) {
			e.vlog("Extracting %v\n", payload)
			e.extractPayload(payload, scratchDir)
		}
	}
	return e.copySharedCaches(scratchDir, destination)
}

func (e *InstallAssistantExtractor) hasCryptexes(plistPath string) (bool, error) {
	print_cmd := "print :Assets:1:OSVersion"
	result, err := exec.Command("PlistBuddy", "-c", print_cmd, plistPath).Output()
	if err != nil {
		return false, fmt.Errorf("couldn't read OS version from %s: %v", plistPath, err)
	}
	majorVersion := strings.Split(string(result), ".")[0]
	if v, err := strconv.Atoi(majorVersion); err != nil {
		return false, fmt.Errorf("couldn't parse major version %s:%v", majorVersion, err)
	} else {
		return v >= 13, nil
	}
}

func (e *InstallAssistantExtractor) extractCryptexDMG(cryptexPath, dmgPath string) error {
	inp := C.CString("")
	defer C.free(unsafe.Pointer(inp))
	cryptex := C.CString(cryptexPath)
	defer C.free(unsafe.Pointer(cryptex))
	result := C.CString(dmgPath)
	defer C.free(unsafe.Pointer(result))

	ri := C.RawImage{unknown1: 0, unknown2: 0, input: inp, output: result, patch: cryptex, not_cryptex_cache: 0, threads: 0, verbose: 1}
	if exitCode := C.RawImagePatch(&ri); exitCode != 0 {
		return fmt.Errorf("RawImagePatch failed with %d", exitCode)
	}
	return nil
}
func (e *InstallAssistantExtractor) extractCachesFromCryptexes(cryptexesPath string, destination string) error {
	scratchDir, err := os.MkdirTemp(e.scratchDir, "cryptex_dmg")
	if err != nil {
		return err
	}
	defer os.RemoveAll(scratchDir)
	files, err := os.ReadDir(cryptexesPath)
	if err != nil {
		return err
	}
	cryptexMountpoint := path.Join(scratchDir, "cryptex")
	for _, f := range files {
		cryptex := path.Join(cryptexesPath, f.Name())
		dmgPath := path.Join(scratchDir, f.Name()+".dmg")
		e.extractCryptexDMG(cryptex, dmgPath)
		e.vlog("Mounting %s at %s\n", cryptex, dmgPath)
		mountDMG(dmgPath, cryptexMountpoint)
		defer unmountDMG(cryptexMountpoint)
		if err := e.copySharedCaches(cryptexMountpoint, destination); err != nil {
			return err
		}
	}
	return nil
}

func (e *InstallAssistantExtractor) Extract(installerPath string, destination string, verbose bool) error {
	e.verbose = verbose
	scratchDir, err := os.MkdirTemp("", "install_assistant")
	defer os.RemoveAll(scratchDir)
	if err != nil {
		return err
	}
	e.scratchDir = scratchDir
	expandedPath := path.Join(scratchDir, "installer")
	e.vlog("Expanding installer to %v\n", expandedPath)
	if err := e.expandInstaller(installerPath, expandedPath); err != nil {
		return fmt.Errorf("expand installer: %v", err)
	}
	dmgPath := path.Join(expandedPath, "SharedSupport.dmg")
	if !pathExists(dmgPath) {
		return fmt.Errorf("couldn't find SharedSupport.dmg at %v", dmgPath)
	}

	dmgMountpoint := path.Join(scratchDir, "shared_support")
	e.vlog("Mounting %v at %v\n", dmgPath, dmgMountpoint)
	if err := mountDMG(dmgPath, dmgMountpoint); err != nil {
		return fmt.Errorf("mount %v: %v", dmgPath, err)
	}
	defer unmountDMG(dmgMountpoint)
	zipsPath := path.Join(dmgMountpoint, "com_apple_MobileAsset_MacSoftwareUpdate")
	if !pathExists(zipsPath) {
		return fmt.Errorf("couldn't find com_apple_MobileAsset_MacSoftwareUpdate on SharedSupport.dmg")
	}
	hasCryptexes, err := e.hasCryptexes(path.Join(zipsPath, "com_apple_MobileAsset_MacSoftwareUpdate.xml"))
	if err != nil {
		return fmt.Errorf("couldn't determine system version: %v", err)
	}
	zips, err := filepath.Glob(path.Join(zipsPath, "*.zip"))
	if err != nil {
		// "The only possible returned error is ErrBadPattern" so treat
		// this like a programmer error.
		log.Fatalf("Failed to glob %v", path.Join(zipsPath, "*.zip"))
	}
	if !hasCryptexes {
		payloadsPath := path.Join(scratchDir, "assets")
		for _, zipFile := range zips {
			archive, err := zip.OpenReader(zipFile)
			if err != nil {
				return fmt.Errorf("couldn't read %v: %v", zipFile, err)
			}
			defer archive.Close()
			e.vlog("Unarchiving %v\n", zipFile)
			if err := e.unarchiveFilesMatching(archive, payloadsPath, "AssetData/payloadv2/payload.0??"); err != nil {
				return fmt.Errorf("couldn't unarchive payloads from %v", zipFile)
			}
		}
		if err := e.extractCachesFromPayloads(payloadsPath, destination); err != nil {
			return fmt.Errorf("couldn't extract caches from %v: %v", payloadsPath, err)
		}
	} else {
		cryptexesPath := path.Join(scratchDir, "cryptexes")
		for _, zipFile := range zips {
			archive, err := zip.OpenReader(zipFile)
			if err != nil {
				return fmt.Errorf("couldn't read %v: %v", zipFile, err)
			}
			defer archive.Close()
			e.vlog("Unarchiving %v\n", zipFile)
			if err := e.unarchiveFilesMatching(archive, cryptexesPath, "AssetData/payloadv2/image_patches/cryptex-system-*"); err != nil {
				return fmt.Errorf("couldn't unarchive cryptexes from %v", zipFile)
			}
		}
		if err := e.extractCachesFromCryptexes(cryptexesPath, destination); err != nil {
			return err
		}
	}
	return nil
}

// Extractor
type IPSWExtractor struct {
	verbose bool
}

func (e *IPSWExtractor) vlog(format string, args ...interface{}) {
	if !e.verbose {
		return
	}
	fmt.Printf(format, args...)
}

func (e *IPSWExtractor) mountSystemDMG(ipswPath string, tempDir string) (string, error) {
	r, err := zip.OpenReader(ipswPath)
	if err != nil {
		return "", err
	}
	defer r.Close()
	dmgPath := ""
	for _, f := range r.File {
		if f.Name == "BuildManifest.plist" {
			manifest, err := os.Create(path.Join(tempDir, f.Name))
			if err != nil {
				return "", err
			}
			if err := extractFileToPath(f, path.Join(tempDir, f.Name)); err != nil {
				return "", err
			}
			path, err := e.getSystemDMGPath(manifest.Name())
			if err != nil {
				return "", err
			}
			dmgPath = path
		}
	}
	if dmgPath == "" {
		return "", errors.New("couldn't find build manifest")
	}
	for _, f := range r.File {
		if filepath.Base(f.Name) == dmgPath {
			dmgPath := path.Join(tempDir, f.Name)
			if err := extractFileToPath(f, dmgPath); err != nil {
				return "", err
			}
			dmgMountpoint := path.Join(tempDir, "Root")
			e.vlog("Mounting %v at %v\n", dmgPath, dmgMountpoint)
			if err := mountDMG(dmgPath, dmgMountpoint); err != nil {
				return "", err
			}
			return dmgMountpoint, nil
		}
	}
	return "", fmt.Errorf("%v not present in %v", dmgPath, ipswPath)
}

func (e *IPSWExtractor) getSystemDMGPath(manifest string) (string, error) {
	print_cmd := "print :BuildIdentities:1:Manifest:Cryptex1,SystemOS:Info:Path"
	result, err := exec.Command("PlistBuddy", "-c", print_cmd, manifest).Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(result)), nil
}

func (e *IPSWExtractor) Extract(ipswPath string, destination string, verbose bool) error {
	e.verbose = verbose
	scratchDir, err := os.MkdirTemp("", "ipswExtract")
	defer os.RemoveAll(scratchDir)
	if err != nil {
		return err
	}
	e.vlog("Extracting and mounting system disk:\n")
	system, err := e.mountSystemDMG(ipswPath, scratchDir)
	if err != nil {
		return fmt.Errorf("couldn't mount system DMG: %v", err)
	}
	defer unmountDMG(system)
	e.vlog("System mounted at %v\n", system)
	e.vlog("Extracting shared caches:\n")
	cachesPath := path.Join(system, "System/Library/dyld")
	if !pathExists(cachesPath) {
		return errors.New("couldn't find /System/Library/dyld")
	}
	caches, err := filepath.Glob(path.Join(cachesPath, "dyld_shared_cache*"))
	if err != nil {
		// "The only possible returned error is ErrBadPattern" so treat
		// this like a programmer error.
		log.Fatalf("Failed to glob %v", path.Join(cachesPath, "dyld_shared_cache*"))
	}
	for _, cache := range caches {
		src, err := os.Open(cache)
		if err != nil {
			return err
		}
		defer src.Close()
		filename := path.Base(cache)
		dst, err := os.Create(path.Join(destination, filename))
		if err != nil {
			return err
		}
		defer dst.Close()
		e.vlog("Extracted %v\n", filename)
		if _, err := io.Copy(dst, src); err != nil {
			return err
		}
	}
	return nil
}

const (
	IPSW      int = 0
	Installer int = 1
)

func ExtractCache(format int, archive string, destination string, verbose bool) error {
	var e Extractor
	switch format {
	case IPSW:
		e = &IPSWExtractor{}
	case Installer:
		e = &InstallAssistantExtractor{}
	default:
		return fmt.Errorf("unknown format %v", format)
	}
	return e.Extract(archive, destination, verbose)
}
