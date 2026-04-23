package system

import (
	"github.com/rmay/nuxvm/pkg/vm"
)

// Machine represents the complete virtual computer (VM + Hardware).
type Machine struct {
	CPU    *vm.VM
	System *System
}

func NewMachine(program []byte, trace ...bool) *Machine {
	cpu := vm.NewVM(program, trace...)
	sys := NewSystem()
	sys.SetMemory(cpu.Memory())
	cpu.SetBus(sys)
	
	return &Machine{
		CPU:    cpu,
		System: sys,
	}
}

// Tick executes the CPU until it yields or halts.
// It returns whether the CPU is still running.
func (m *Machine) Tick() (bool, error) {
	if !m.CPU.Running() {
		return false, nil
	}

	m.CPU.ClearYield()
	
	// Run until yield or halt
	for m.CPU.Running() && !m.CPU.Yielded() {
		_, err := m.CPU.Step()
		if err != nil {
			return false, err
		}
	}
	
	return m.CPU.Running(), nil
}

// Input methods (proxy to system and trigger vectors)

func (m *Machine) PushKey(key int32) error {
	m.System.SetKey(key)
	return m.CPU.TriggerVector(4) // Controller Vector
}

func (m *Machine) PushButton(mask uint32) error {
	m.System.SetButton(mask)
	return m.CPU.TriggerVector(4) // Controller Vector
}

func (m *Machine) MoveMouse(x, y int32) error {
	m.System.SetMouse(x, y, m.System.mouseButton)
	return m.CPU.TriggerVector(5) // Mouse Vector
}

func (m *Machine) PushMouseButton(mask uint32) error {
	m.System.SetMouse(m.System.mouseX, m.System.mouseY, mask)
	return m.CPU.TriggerVector(5) // Mouse Vector
}

// VBlank triggers the screen vector (2). Called every frame.
func (m *Machine) VBlank() error {
	return m.CPU.TriggerVector(2) // Screen Vector
}

// TriggerAudio triggers the audio vector (3).
func (m *Machine) TriggerAudio() error {
	return m.CPU.TriggerVector(3) // Audio Vector
}

