package system

import (
	"github.com/rmay/nuxvm/pkg/vm"
)

// Machine represents the complete virtual computer (VM + Hardware).
type Machine struct {
	CPU       *vm.VM
	System    *System
	inputQueue chan InputEvent // Per-machine input queue (buffered, cap 64)
}

func NewMachine(program []byte, memSize uint32, trace ...bool) *Machine {
	var cpu *vm.VM
	if memSize > 0 {
		cpu = vm.NewVMWithMemorySize(program, memSize, trace...)
	} else {
		cpu = vm.NewVM(program, trace...)
	}
	sys := NewSystem()
	sys.SetMemory(cpu.Memory())

	// Wire vector callbacks: when Lux code writes to a vector register,
	// the Bus calls back to set/get the vector in the CPU.
	sys.SetVectorCallbacks(
		func(index int) uint32 { return cpu.GetVector(index) },
		func(index int, addr uint32) { cpu.SetVector(index, addr) },
	)

	cpu.SetBus(sys)

	// Start all OS service goroutines
	sys.Services.StartAllServices()

	// Wire sandbox resolver for file operations
	sys.Services.SetSandboxResolver(sys.resolvePath)

	// Wire audio playback through the SoundServer service
	sys.SoundHandler = func(soundID int32) {
		// Non-blocking send to sound server
		select {
		case sys.Services.soundChan <- SoundMsg{Command: "play_sound", SoundID: soundID}:
			// Don't wait for reply to avoid blocking the VM
		default:
			// SoundServer busy or not running, drop event
		}
	}

	return &Machine{
		CPU:        cpu,
		System:     sys,
		inputQueue: make(chan InputEvent, 64),
	}
}

// NewMachineSharedServices builds a Machine that shares a ServiceManager
// (window list, layout, input queue, sandbox resolver) with an already-running
// Machine. Used by the Cloister launcher to spawn additional Lux programs in
// their own VM while keeping a single window manager.
//
// The new Machine has its own CPU, memory, text/screen state, and vectors —
// only Services is shared. Services goroutines are NOT restarted; the caller
// must have already started them on the shared instance.
func NewMachineSharedServices(program []byte, memSize uint32, services *ServiceManager, trace ...bool) *Machine {
	var cpu *vm.VM
	if memSize > 0 {
		cpu = vm.NewVMWithMemorySize(program, memSize, trace...)
	} else {
		cpu = vm.NewVM(program, trace...)
	}
	sys := NewSystem()
	// Drop the auto-created Services in favor of the shared one. The discarded
	// Services has unstarted goroutine channels; they're GC'd when this scope
	// exits.
	sys.Services = services
	sys.SetMemory(cpu.Memory())
	sys.SetVectorCallbacks(
		func(index int) uint32 { return cpu.GetVector(index) },
		func(index int, addr uint32) { cpu.SetVector(index, addr) },
	)
	cpu.SetBus(sys)

	sys.SoundHandler = func(soundID int32) {
		select {
		case sys.Services.soundChan <- SoundMsg{Command: "play_sound", SoundID: soundID}:
		default:
		}
	}

	return &Machine{
		CPU:        cpu,
		System:     sys,
		inputQueue: make(chan InputEvent, 64),
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
	return m.CPU.TriggerVector(ControllerVectorIdx)
}

func (m *Machine) PushButton(mask uint32) error {
	m.System.SetButton(mask)
	return m.CPU.TriggerVector(ControllerVectorIdx)
}

func (m *Machine) MoveMouse(x, y int32) error {
	m.System.SetMouse(x, y, m.System.mouseButton)
	return m.CPU.TriggerVector(MouseVectorIdx)
}

func (m *Machine) PushMouseButton(mask uint32) error {
	m.System.SetMouse(m.System.mouseX, m.System.mouseY, mask)
	return m.CPU.TriggerVector(MouseVectorIdx)
}

// SetSandboxRoot pins the File device's filesystem sandbox to dir. All File
// device operations are resolved relative to this path and rejected if they
// escape (via .., absolute paths, or symlinks).
func (m *Machine) SetSandboxRoot(dir string) error {
	return m.System.SetSandboxRoot(dir)
}

// VBlank triggers the screen vector. Called every frame.
func (m *Machine) VBlank() error {
	return m.CPU.TriggerVector(ScreenVectorIdx)
}

// TriggerAudio triggers the audio vector.
func (m *Machine) TriggerAudio() error {
	return m.CPU.TriggerVector(AudioVectorIdx)
}

// Services returns the OS service manager for IPC.
func (m *Machine) Services() *ServiceManager {
	return m.System.Services
}

// QueueKeyDown queues a keyboard event for this machine.
func (m *Machine) QueueKeyDown(keyCode int32) {
	select {
	case m.inputQueue <- InputEvent{Type: InputKeyDown, KeyCode: keyCode}:
	default:
		// queue full, drop event
	}
}

// QueueMouseButton queues a mouse button event for this machine.
func (m *Machine) QueueMouseButton(x, y int32, btn uint32, down bool) {
	evtType := InputMouseUp
	if down {
		evtType = InputMouseDown
	}
	select {
	case m.inputQueue <- InputEvent{Type: evtType, MouseX: x, MouseY: y, MouseBtn: btn}:
	default:
	}
}

// QueueMouseMove queues a mouse move event for this machine.
func (m *Machine) QueueMouseMove(x, y int32) {
	select {
	case m.inputQueue <- InputEvent{Type: InputMouseMove, MouseX: x, MouseY: y}:
	default:
	}
}

// DrainInputEvents polls and dispatches all pending input events to the VM.
// Called each frame before machine.Tick() to feed buffered input to the VM.
func (m *Machine) DrainInputEvents() {
	for {
		var evt *InputEvent
		select {
		case e := <-m.inputQueue:
			evt = &e
		default:
			return
		}

		if evt == nil {
			break
		}

		switch evt.Type {
		case InputKeyDown:
			m.System.SetKey(evt.KeyCode)
			_ = m.CPU.TriggerVector(ControllerVectorIdx)
		case InputKeyUp:
			m.System.SetKey(0) // clear key on release
			_ = m.CPU.TriggerVector(ControllerVectorIdx)
		case InputMouseMove:
			m.System.SetMouse(evt.MouseX, evt.MouseY, m.System.MouseButton())
			_ = m.CPU.TriggerVector(MouseVectorIdx)
		case InputMouseDown, InputMouseUp:
			m.System.SetMouse(evt.MouseX, evt.MouseY, evt.MouseBtn)
			_ = m.CPU.TriggerVector(MouseVectorIdx)
		}
	}
}

