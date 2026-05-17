package system

import (
	"testing"
	"github.com/rmay/nuxvm/pkg/vm"
)

func TestSCISetWindowTitle(t *testing.T) {
	sys := NewSystem()
	mem := make([]byte, vm.HeadlessBaseAddress+1024)
	sys.SetMemory(mem)

	// Set up ServiceManager with a TitleHandler
	var capturedTitle string
	sys.Services = NewServiceManager()
	sys.Services.TitleHandler = func(title string) {
		capturedTitle = title
	}

	// Write title string to VM memory
	title := "Hello Lux!"
	titleAddr := uint32(vm.HeadlessBaseAddress) + 10
	copy(mem[titleAddr:], []byte(title+"\x00"))

	// Trigger SCISetWindowTitle
	sys.Write(sciPort+4, int32(SCISetWindowTitle)) // sciCommand
	sys.Write(sciPort+8, int32(titleAddr))        // sciArg1
	sys.Write(sciPort+12, 0)                      // sciArg2 triggers handleSCICommand

	if capturedTitle != title {
		t.Errorf("Expected title %q, got %q", title, capturedTitle)
	}
}
