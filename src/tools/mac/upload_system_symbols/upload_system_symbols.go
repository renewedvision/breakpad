/* Copyright 2014 Google LLC

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

 * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
 * Neither the name of Google LLC nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Tool upload_system_symbols generates and uploads Breakpad symbol files for OS X system libraries.

This tool shells out to the dump_syms and symupload Breakpad tools. In its default mode, this
will find all dynamic libraries on the system, run dump_syms to create the Breakpad symbol files,
and then upload them to Google's crash infrastructure.

The tool can also be used to only dump libraries or upload from a directory. See -help for more
information.

Both i386 and x86_64 architectures will be dumped and uploaded.
*/
package main

import (
	"flag"
	"log"
	"os"

	"upload_system_symbols/archive"
	"upload_system_symbols/breakpad"
)

var (
	breakpadTools    = flag.String("breakpad-tools", "out/Release/", "Path to the Breakpad tools directory, containing dump_syms and symupload.")
	uploadOnlyPath   = flag.String("upload-from", "", "Upload a directory of symbol files that has been dumped independently.")
	dumpOnlyPath     = flag.String("dump-to", "", "Dump the symbols to the specified directory, but do not upload them.")
	systemRoot       = flag.String("system-root", "", "Path to the root of the macOS system whose symbols will be dumped. Mutually exclusive with --installer and --ipsw.")
	dumpArchitecture = flag.String("arch", "", "The CPU architecture for which symbols should be dumped. If not specified, dumps all architectures.")
	apiKey           = flag.String("api-key", "", "API key to use. If this is present, the `sym-upload-v2` protocol is used.\nSee https://chromium.googlesource.com/breakpad/breakpad/+/HEAD/docs/sym_upload_v2_protocol.md or\n`symupload`'s help for more information.")
	installer        = flag.String("installer", "", "Path to macOS installer. Mutually exclusive with --system-root and --ipsw.")
	ipsw             = flag.String("ipsw", "", "Path to macOS IPSW. Mutually exclusive with --system-root and --installer.")
)

var (
	// uploadServersV1 are the list of servers to which symbols should be
	// uploaded when using the V1 protocol.
	uploadServersV1 = []string{
		"https://clients2.google.com/cr/symbol",
		"https://clients2.google.com/cr/staging_symbol",
	}
	// uploadServersV2 are the list of servers to which symbols should be
	// uploaded when using the V2 protocol.
	uploadServersV2 = []string{
		"https://staging-crashsymbolcollector-pa.googleapis.com",
		"https://prod-crashsymbolcollector-pa.googleapis.com",
	}

	// uploadServers are the list of servers that should be used, accounting
	// for whether v1 or v2 protocol is used.
	uploadServers = uploadServersV1
)

func main() {
	flag.Parse()
	log.SetFlags(0)

	// If `apiKey` is set, we're using the v2 protocol.
	if len(*apiKey) > 0 {
		uploadServers = uploadServersV2
	}

	var uq *breakpad.UploadQueue

	if *uploadOnlyPath != "" {
		// -upload-from specified, so handle that case early.
		uq = breakpad.StartUploadQueue()
		breakpad.UploadFromDirectory(*uploadOnlyPath, uq)
		uq.Wait()
		return
	}

	dumpPath := *dumpOnlyPath
	if *dumpOnlyPath == "" {
		// If -dump-to was not specified, then run the upload pipeline and create
		// a temporary dump output directory.
		uq = breakpad.StartUploadQueue()

		if p, err := os.MkdirTemp("", "upload_system_symbols"); err != nil {
			log.Fatalf("Failed to create temporary directory: %v", err)
		} else {
			dumpPath = p
			defer os.RemoveAll(p)
		}
	}

	tempDir, err := os.MkdirTemp("", "systemRoots")
	if err != nil {
		log.Fatalf("Failed to create temporary directory: %v", err)
	}
	defer os.RemoveAll(tempDir)
	roots := getSystemRoots(tempDir)

	if *dumpOnlyPath != "" {
		// -dump-to specified, so make sure that the path is a directory.
		if fi, err := os.Stat(*dumpOnlyPath); err != nil {
			log.Fatalf("-dump-to location: %v", err)
		} else if !fi.IsDir() {
			log.Fatal("-dump-to location is not a directory")
		}
	}

	dq := breakpad.StartDumpQueue(roots, dumpPath, uq)
	dq.Wait()
	if uq != nil {
		uq.Wait()
	}
}

// getSystemRoots returns which system roots should be dumped from the parsed
// flags, extracting them if necessary.
func getSystemRoots(tempDir string) []string {
	hasInstaller := len(*installer) > 0
	hasIPSW := len(*ipsw) > 0
	hasRoot := len(*systemRoot) > 0

	if hasInstaller {
		if hasIPSW || hasRoot {
			log.Fatalf("--installer, --ipsw, and --system-root are mutually exclusive")
		}
		if rs, err := archive.ExtractSystems(archive.Installer, *installer, tempDir); err != nil {
			log.Fatalf("Couldn't extract installer at %s: %v", *installer, err)
		} else {
			return rs
		}
	} else if hasIPSW {
		if hasRoot {
			log.Fatalf("--installer, --ipsw, and --system-root are mutually exclusive")
		}
		if rs, err := archive.ExtractSystems(archive.IPSW, *ipsw, tempDir); err != nil {
			log.Fatalf("Couldn't extract IPSW at %s: %v", *ipsw, err)
		} else {
			return rs
		}
	} else if hasRoot {
		return []string{*systemRoot}
	}
	log.Fatal("Need a --system-root, --installer, or --ipsw to dump symbols for")
	return []string{}
}
