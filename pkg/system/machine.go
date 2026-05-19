package system

import (
	"fmt"
	"os"

	"github.com/rmay/nuxvm/pkg/vm"
)

// Machine represents the complete virtual computer (VM + Hardware).
type Machine struct {
	CPU    *vm.VM
	System *System
}

func NewMachine(program []byte, baseAddress uint32, memSize uint32, trace ...bool) *Machine {
	var cpu *vm.VM
	if memSize > 0 {
		cpu = vm.NewVMWithMemorySize(program, baseAddress, memSize, trace...)
	} else {
		cpu = vm.NewVM(program, baseAddress, trace...)
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
		CPU:    cpu,
		System: sys,
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
func NewMachineSharedServices(program []byte, baseAddress uint32, memSize uint32, services *ServiceManager, trace ...bool) *Machine {
	var cpu *vm.VM
	if memSize > 0 {
		cpu = vm.NewVMWithMemorySize(program, baseAddress, memSize, trace...)
	} else {
		cpu = vm.NewVM(program, baseAddress, trace...)
	}
	// Skip the 5 MB screenPixels fallback — this VM shares a ServiceManager
	// that has real windows, so getActiveFramebuffer always resolves to a
	// window FrameBuf.
	sys := NewSystemNoFallback()
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
		CPU:    cpu,
		System: sys,
	}
}

// Tick executes the CPU until it yields or halts.
// It returns whether the CPU is still running.
func (m *Machine) Tick() (bool, error) {
	if m.System.Services != nil && m.System.Services.HasModal() {
		fb := m.System.getActiveFramebuffer()
		w := m.System.getScreenWidth()
		h := m.System.getScreenHeight()
		m.System.Services.DrawModal(fb, w, h)
		if win := m.System.Services.GetActiveWindow(); win != nil {
			win.Dirty = true
		}
		return true, nil
	}

	if m.CPU.Halted() {
		return false, nil
	}

	m.CPU.SetRunning(true)
	m.CPU.ClearYield()
	m.System.yielded = false

	// Run until yield or halt
	for m.CPU.Running() && !m.CPU.Yielded() && !m.System.yielded {
		_, err := m.CPU.Step()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Machine: Tick error at PC=0x%X: %v\n", m.CPU.PC(), err)
			fmt.Fprintf(os.Stderr, "Machine: Stack Dump (top 32): %v\n", m.CPU.StackDump(32))
			fmt.Fprintf(os.Stderr, "Machine: ReturnStack Dump: %v\n", m.CPU.ReturnStack())
			pc := int(m.CPU.PC())
			mem := m.CPU.Memory()
			lo, hi := pc-32, pc+32
			if lo < 0 {
				lo = 0
			}
			if hi > len(mem) {
				hi = len(mem)
			}
			fmt.Fprintf(os.Stderr, "Machine: bytes near PC (0x%X..0x%X):\n  ", lo, hi)
			for i := lo; i < hi; i++ {
				if i == pc {
					fmt.Fprintf(os.Stderr, "*%02X", mem[i])
				} else {
					fmt.Fprintf(os.Stderr, " %02X", mem[i])
				}
			}
			fmt.Fprintln(os.Stderr)
			recent := m.CPU.RecentPCs()
			fmt.Fprintf(os.Stderr, "Machine: recent PCs (oldest..newest, %d):\n", len(recent))
			for i, p := range recent {
				if i > 0 && i%8 == 0 {
					fmt.Fprintln(os.Stderr)
				}
				fmt.Fprintf(os.Stderr, " 0x%X", p)
			}
			fmt.Fprintln(os.Stderr)
			return false, err
		}
	}

	// Tick children
	for _, child := range m.System.childMachines {
		_, err := child.Tick()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Child Machine Tick error: %v\n", err)
		}
	}

	return !m.CPU.Halted(), nil
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
	m.QueueKeyDownMods(keyCode, 0)
}

// QueueKeyDownMods queues a keyboard event with modifier flags.
func (m *Machine) QueueKeyDownMods(keyCode int32, mods uint32) {
	select {
	case m.System.inputQueue <- InputEvent{Type: InputKeyDown, KeyCode: keyCode, Modifiers: mods}:
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
	case m.System.inputQueue <- InputEvent{Type: evtType, MouseX: x, MouseY: y, MouseBtn: btn}:
	default:
	}
}

// QueueMouseMove queues a mouse move event for this machine.
func (m *Machine) QueueMouseMove(x, y int32) {
	select {
	case m.System.inputQueue <- InputEvent{Type: InputMouseMove, MouseX: x, MouseY: y}:
	default:
	}
}

// QueueWheel queues a mouse wheel event for this machine.
func (m *Machine) QueueWheel(dx, dy float64) {
	select {
	case m.System.inputQueue <- InputEvent{Type: InputWheel, WheelX: dx, WheelY: dy}:
	default:
	}
}

// DrainInputEvents polls and dispatches all pending input events to the VM.
// Called each frame before machine.Tick() to feed buffered input to the VM.
func (m *Machine) DrainInputEvents() {
	if m.System.Services != nil && m.System.Services.HasModal() {
		for {
			var evt *InputEvent
			select {
			case e := <-m.System.inputQueue:
				evt = &e
			default:
				return
			}
			if !m.System.Services.UpdateModal(evt) {
				return
			}
		}
	}

	var mouseChanged bool
	mouseX, mouseY := m.System.mouseX, m.System.mouseY
	mouseBtn := m.System.MouseButton()

	for {
		var evt *InputEvent
		select {
		case e := <-m.System.inputQueue:
			evt = &e
		default:
			goto done
		}

		if evt == nil {
			break
		}

		switch evt.Type {
		case InputKeyDown:
			m.System.SetKey(evt.KeyCode)
			_ = m.CPU.TriggerVector(ControllerVectorIdx)
			select {
			case m.System.kbdEvents <- *evt:
			default:
			}
		case InputKeyUp:
			m.System.SetKey(0) // clear key on release
			_ = m.CPU.TriggerVector(ControllerVectorIdx)
			select {
			case m.System.kbdEvents <- *evt:
			default:
			}
		case InputMouseMove:
			mouseX, mouseY = evt.MouseX, evt.MouseY
			mouseChanged = true
			select {
			case m.System.mouseEvents <- *evt:
			default:
			}
		case InputMouseDown, InputMouseUp:
			mouseX, mouseY = evt.MouseX, evt.MouseY
			mouseBtn = evt.MouseBtn
			mouseChanged = true
			select {
			case m.System.mouseEvents <- *evt:
			default:
			}
		case InputWheel:
			m.System.SetWheel(int32(evt.WheelY))
			_ = m.CPU.TriggerVector(WheelVectorIdx)
		case InputResize:
			m.System.SetResize(evt.ResizeW, evt.ResizeH)
			_ = m.CPU.TriggerVector(ResizeVectorIdx)
		}
	}

done:
	if mouseChanged {
		m.System.SetMouse(mouseX, mouseY, mouseBtn)
		_ = m.CPU.TriggerVector(MouseVectorIdx)
	}
}
