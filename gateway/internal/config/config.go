package config

import (
	"os"
	"time"

	"gopkg.in/yaml.v3"
)

// Config represents the application configuration
type Config struct {
	Server        ServerConfig        `yaml:"server"`
	KeyDB         KeyDBConfig         `yaml:"keydb"`
	Postgres      PostgresConfig      `yaml:"postgres"`
	Auth          AuthConfig          `yaml:"auth"`
	Observability ObservabilityConfig `yaml:"observability"`
	Logging       LoggingConfig       `yaml:"logging"`
}

// ServerConfig represents HTTP server configuration
type ServerConfig struct {
	HTTPPort     int           `yaml:"http_port"`
	MetricsPort  int           `yaml:"metrics_port"`
	ReadTimeout  time.Duration `yaml:"read_timeout"`
	WriteTimeout time.Duration `yaml:"write_timeout"`
	IdleTimeout  time.Duration `yaml:"idle_timeout"`
}

// KeyDBConfig represents KeyDB/Redis connection configuration
type KeyDBConfig struct {
	Addr     string `yaml:"addr"`
	Password string `yaml:"password"`
	DB       int    `yaml:"db"`
	PoolSize int    `yaml:"pool_size"`
}

// PostgresConfig represents PostgreSQL connection configuration
type PostgresConfig struct {
	DSN             string        `yaml:"dsn"`
	MaxOpenConns    int           `yaml:"max_open_conns"`
	MaxIdleConns    int           `yaml:"max_idle_conns"`
	ConnMaxLifetime time.Duration `yaml:"conn_max_lifetime"`
}

// AuthConfig represents authentication configuration
type AuthConfig struct {
	APIKeyHeader string   `yaml:"api_key_header"`
	ValidAPIKeys []string `yaml:"valid_api_keys"`
}

// ObservabilityConfig represents observability configuration
type ObservabilityConfig struct {
	ServiceName       string  `yaml:"service_name"`
	OTLPEndpoint      string  `yaml:"otlp_endpoint"`
	MetricsEnabled    bool    `yaml:"metrics_enabled"`
	TracingEnabled    bool    `yaml:"tracing_enabled"`
	TracingSampleRate float64 `yaml:"tracing_sample_rate"`
}

// LoggingConfig represents logging configuration
type LoggingConfig struct {
	Level  string `yaml:"level"`
	Format string `yaml:"format"`
}

// Load reads configuration from a YAML file
func Load(filename string) (*Config, error) {
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, err
	}

	var cfg Config
	if err := yaml.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}

	// Apply environment variable overrides
	applyEnvOverrides(&cfg)

	return &cfg, nil
}

// applyEnvOverrides applies environment variable overrides to the configuration
func applyEnvOverrides(cfg *Config) {
	if addr := os.Getenv("KEYDB_ADDR"); addr != "" {
		cfg.KeyDB.Addr = addr
	}
	if dsn := os.Getenv("DB_DSN"); dsn != "" {
		cfg.Postgres.DSN = dsn
	}
	if port := os.Getenv("HTTP_PORT"); port != "" {
		cfg.Server.HTTPPort = parseInt(port, cfg.Server.HTTPPort)
	}
	if level := os.Getenv("LOG_LEVEL"); level != "" {
		cfg.Logging.Level = level
	}
}

func parseInt(s string, defaultVal int) int {
	var val int
	if _, err := parseIntFromString(s); err == nil {
		val = int(parseIntFromString(s))
	}
	return val
}

func parseIntFromString(s string) (int, error) {
	var val int
	_, err := os.Stat(s)
	if err != nil {
		return 0, err
	}
	return val, nil
}
