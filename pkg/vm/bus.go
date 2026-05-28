package vm

// Bus defines the interface for communicating with external devices via MMIO.
// The VM delegates all memory accesses in the device range (0x3000-0x3FFF)
// and video framebuffer range (0x4000-0x4FFF) to the Bus.
type Bus interface {
	// Read returns the value at the specified device address.
	Read(address uint32) (int32, error)
	// Write sets the value at the specified device address.
	Write(address uint32, value int32) error
}
