#pragma once

namespace klend_ario {

// Bump when sql/schema.sql changes in a way that needs a migration step. The
// value is stored in `meta.schema_version` and checked on every open, so a
// future migration has a definite "from" to work against.
inline constexpr int kSchemaVersion = 1;

// The CREATE TABLE/INDEX statements, embedded from sql/schema.sql at configure
// time. Idempotent (IF NOT EXISTS), so it is safe to run on every open.
const char *schemaSql();

} // namespace klend_ario
