package config

import (
	"os"
	"strconv"

	"gopkg.in/yaml.v3"
)

type Config struct {
	Server   ServerConfig   `yaml:"server"`
	Database DatabaseConfig `yaml:"database"`
	KeyDB    KeyDBConfig    `yaml:"keydb"`
	Worker   WorkerConfig   `yaml:"worker"`
}

type ServerConfig struct {
	Host string `yaml:"host"`
	Port int    `yaml:"port"`
}

type DatabaseConfig struct {
	Host        string `yaml:"host"`
	Port        int    `yaml:"port"`
	User        string `yaml:"user"`
	Password    string `yaml:"password"`
	Database    string `yaml:"database"`
	SSLMode     string `yaml:"sslmode"`
	DSNOverride string `yaml:"-"` // Set from DB_DSN env var
}

func (c *DatabaseConfig) DSN() string {
	if c.DSNOverride != "" {
		return c.DSNOverride
	}
	return "postgres://" + c.User + ":" + c.Password + "@" + c.Host + ":" + strconv.Itoa(c.Port) + "/" + c.Database + "?sslmode=" + c.SSLMode
}

type KeyDBConfig struct {
	Addr     string `yaml:"addr"`
	Password string `yaml:"password"`
	DB       int    `yaml:"db"`
}

type WorkerConfig struct {
	SimulatorBinary string `yaml:"simulator_binary"`
	GDBPortRange    struct {
		Start int `yaml:"start"`
		End   int `yaml:"end"`
	} `yaml:"gdb_port_range"`
	DebugTimeoutSeconds int `yaml:"debug_timeout_seconds"`
}

func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var cfg Config
	if err := yaml.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}

	// Override with environment variables if set
	if v := os.Getenv("SERVER_HOST"); v != "" {
		cfg.Server.Host = v
	}
	if v := os.Getenv("SERVER_PORT"); v != "" {
		if port, err := strconv.Atoi(v); err == nil {
			cfg.Server.Port = port
		}
	}
	if v := os.Getenv("KEYDB_ADDR"); v != "" {
		cfg.KeyDB.Addr = v
	}
	if v := os.Getenv("DB_DSN"); v != "" {
		// Store the DSN directly for use by repository
		cfg.Database.DSNOverride = v
	}
	if v := os.Getenv("SIMULATOR_BINARY"); v != "" {
		cfg.Worker.SimulatorBinary = v
	}

	return &cfg, nil
}

func Default() *Config {
	return &Config{
		Server: ServerConfig{
			Host: "0.0.0.0",
			Port: 8080,
		},
		Database: DatabaseConfig{
			Host:     "localhost",
			Port:     5432,
			User:     "lab",
			Password: "secret",
			Database: "lab",
			SSLMode:  "disable",
		},
		KeyDB: KeyDBConfig{
			Addr:     "localhost:6379",
			Password: "",
			DB:       0,
		},
		Worker: WorkerConfig{
			SimulatorBinary:     "./stm32sim",
			DebugTimeoutSeconds: 3600,
			GDBPortRange: struct {
				Start int `yaml:"start"`
				End   int `yaml:"end"`
			}{
				Start: 3333,
				End:   3343,
			},
		},
	}
}
