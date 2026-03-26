# Хранилище данных: PostgreSQL

PostgreSQL используется для долговременного хранения информации о заданиях и GDB-сессиях. 

## Таблица jobs

```sql
CREATE TABLE jobs (
    job_id VARCHAR(26) PRIMARY KEY,
    user_id VARCHAR(255) NOT NULL,
    sha256 CHAR(64) NOT NULL,
    state VARCHAR(20) NOT NULL,
    worker_id VARCHAR(255),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    started_at TIMESTAMPTZ,
    finished_at TIMESTAMPTZ,
    timeout_seconds INT NOT NULL DEFAULT 30,
    error_text TEXT,
    -- Новые поля для отладки
    debug_mode BOOLEAN DEFAULT false,
    gdb_port INT,
    gdb_host VARCHAR(255),
    gdb_connected BOOLEAN DEFAULT false,
    gdb_connected_at TIMESTAMPTZ,
    metadata JSONB,

    INDEX idx_user_id (user_id),
    INDEX idx_state (state),
    INDEX idx_debug (debug_mode)
);
```

## Таблица debug_sessions

```sql
CREATE TABLE debug_sessions (
    id BIGSERIAL PRIMARY KEY,
    job_id VARCHAR(26) NOT NULL,
    user_id VARCHAR(255) NOT NULL,
    gdb_port INT,
    client_ip INET,
    connected_at TIMESTAMPTZ,
    disconnected_at TIMESTAMPTZ,
    commands_executed INT DEFAULT 0,

    FOREIGN KEY (job_id) REFERENCES jobs(job_id),
    INDEX idx_job_id (job_id)
);
```

Эти таблицы позволяют анализировать историю запусков и использование отладки. 