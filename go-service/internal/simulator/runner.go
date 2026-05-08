package simulator

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"
)

// SimOutput is the JSON structure produced by stm32sim --json.
type SimOutput struct {
	HaltReason       string             `json:"halt_reason"`
	Cycles           uint64             `json:"cycles"`
	Registers        map[string]uint64  `json:"registers"`
	UartTx           string             `json:"uart_tx"`
	UartEvents       []UartEvent        `json:"uart_events"`
	ProfilerHandlers []ProfilerHandler  `json:"profiler_handlers"`
	ProfilerInstrs   []ProfilerInstr    `json:"profiler_instrs"`
	// Errors are populated from stderr, not from the simulator JSON
	Errors []string `json:"errors,omitempty"`
}

type UartEvent struct {
	Cycle uint64 `json:"cycle"`
	Dir   string `json:"dir"`
	Byte  uint8  `json:"byte"`
	Char  string `json:"char"`
}

type ProfilerHandler struct {
	Name        string `json:"name"`
	Calls       uint64 `json:"calls"`
	TotalCycles uint64 `json:"total_cycles"`
	AvgCycles   uint64 `json:"avg_cycles"`
	MaxCycles   uint64 `json:"max_cycles"`
}

type ProfilerInstr struct {
	Name  string `json:"name"`
	Count uint64 `json:"count"`
}

// RunResult is the full result including wall-clock metadata.
type RunResult struct {
	Status         string    `json:"status"`
	WallDurationMs int64     `json:"wall_duration_ms"`
	CompletedAt    time.Time `json:"completed_at"`
	Sim            SimOutput `json:"sim"`
	// ErrorMessage holds runner-level errors (process spawn, timeout, etc.)
	ErrorMessage string `json:"error_message,omitempty"`
}

type Config struct {
	BinaryPath string
	MaxCycles  uint64
	Timeout    time.Duration
}

// Run executes the simulator on the given firmware binary and returns the result.
func Run(ctx context.Context, cfg Config, firmwarePath string) RunResult {
	start := time.Now()

	runCtx, cancel := context.WithTimeout(ctx, cfg.Timeout)
	defer cancel()

	args := []string{firmwarePath, "--json"}
	if cfg.MaxCycles > 0 {
		args = append(args, "--max-cycles", fmt.Sprintf("%d", cfg.MaxCycles))
	}

	cmd := exec.CommandContext(runCtx, cfg.BinaryPath, args...)

	var stdoutBuf, stderrBuf bytes.Buffer
	cmd.Stdout = &stdoutBuf
	cmd.Stderr = &stderrBuf

	err := cmd.Run()
	dur := time.Since(start).Milliseconds()

	result := RunResult{
		WallDurationMs: dur,
		CompletedAt:    time.Now(),
	}

	if runCtx.Err() == context.DeadlineExceeded {
		result.Status = "timeout"
		result.ErrorMessage = fmt.Sprintf("simulator timed out after %v", cfg.Timeout)
		return result
	}

	// Collect stderr lines as errors, skipping known startup diagnostics.
	stderrStr := strings.TrimSpace(stderrBuf.String())
	var stderrLines []string
	for _, line := range strings.Split(stderrStr, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		if line == "Simulator reset" || strings.HasPrefix(line, "Loaded ") {
			continue
		}
		stderrLines = append(stderrLines, line)
	}

	if err != nil {
		var exitErr *exec.ExitError
		if ok := isExitError(err, &exitErr); ok {
			result.Status = "crash"
			result.ErrorMessage = fmt.Sprintf("simulator exited with code %d", exitErr.ExitCode())
		} else {
			result.Status = "error"
			result.ErrorMessage = err.Error()
		}
		result.Sim.Errors = stderrLines
		return result
	}

	// Parse stdout JSON
	var simOut SimOutput
	if jsonErr := json.Unmarshal(stdoutBuf.Bytes(), &simOut); jsonErr != nil {
		result.Status = "error"
		result.ErrorMessage = fmt.Sprintf("failed to parse simulator output: %v", jsonErr)
		result.Sim.Errors = stderrLines
		return result
	}

	simOut.Errors = stderrLines
	result.Status = "ok"
	result.Sim = simOut
	return result
}

// WriteTempFile writes data to a temp file and returns its path.
// Caller is responsible for removing the file.
func WriteTempFile(data []byte) (string, error) {
	f, err := os.CreateTemp("", "stm32sim-*.bin")
	if err != nil {
		return "", err
	}
	defer f.Close()
	_, err = f.Write(data)
	return f.Name(), err
}

func isExitError(err error, target **exec.ExitError) bool {
	var e *exec.ExitError
	if ok := asExitError(err, &e); ok {
		*target = e
		return true
	}
	return false
}

func asExitError(err error, target **exec.ExitError) bool {
	e, ok := err.(*exec.ExitError)
	if ok {
		*target = e
	}
	return ok
}
