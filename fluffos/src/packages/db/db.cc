/******************************************************************************
 ****
 ****    Database package for the MudOS driver
 ****
 ****    History:
 ****        Sometime:
 ****            Descartes@Nightmare created and added mSQL support
 ****
 ****        Feb 1999:
 ****            Andrew@Nanvaent restructured to add MySQL support and a
 ****            framework for other databases to be added.
 ****
 ****        Jul 2000:
 ****            Andrew@Nanvaent's work included in the MudOS proper
 ****
 ****        Apr 2006:
 ****            Ajandurah@Demonslair added SQLite v3 support, defining
 ****            USE_SQLITE3 in local_options will enable it.
 ****
 ****        Feb 2009:
 ****            Ajandurah@Demonslair added SQLite v2 support, defining
 ****            USE_SQLITE2 in local_options will enable it.
 ****
 ****    Notes:
 ****      . This package has been restructured so that it can be compiled into
 ****        a driver without any database types defined so that you can write
 ****        stuff without necessarily having the database.
 ****
 ****      . No database type has been added that supports commit or rollback,
 ****        so these functions have not been fully implemented, particularly
 ****        with regard to error handling.
 ****
 ****      . Support for multiple database types is present, if obscure.  When
 ****        you have multiple types you should have DEFAULT_DB defined to be
 ****        the default one, and USE_MYSQL/USE_MSQL should be defined to be
 ****        numbers in the local_options file or equivalent, e.g.:
 ****            #define USE_MSQL 1
 ****            #define USE_MYSQL 2
 ****            #define DEFAULT_DB USE_MSQL
 ****
 ****        The value that you defined it to will be that expected when you
 ****        make a call to db_connect( ... ) as the fourth argument.  Without
 ****        the fourth argument, the value used will be that for DEFAULT_DB.
 ****
 ****      . Adding another database type should involve:
 ****        + picking your own define name
 ****        + editing db.h and adding an appropriate member to the dbconn_t
 ****          union
 ****        + adding a dbdefn_t definition for it in this file
 ****        + playing around with the code for deciding between databases in
 ****          f_db_connect()
 ****        + writing all the required interface functions as you've defined
 ****          for the dbdefn_t structure.  Minimum requirements would be
 ****          connect, close, fetch and execute and cleanup if you need to
 ****          cleanup memory allocated between searches.
 ****
 ****    TODO:
 ****      . Decent Error Message reporting
 ****      . Function for showing the current connections (incomplete)
 ****      . Standardise on return values (only db_exec is nonstandard)
 ****      . Documentation
 ****      . Add more databases
 ****
 ******************************************************************************/

#include "base/package_api.h"

#include "packages/db/db.h"

#include "packages/core/file.h"
#include "packages/core/outbuf.h"

#ifdef PACKAGE_ASYNC
#include <pthread.h>
#endif

static int db_conn_alloc, db_conn_used;
static db_t *db_conn_list;

db_t *find_db_conn(int);
static int create_db_conn();
static void free_db_conn(db_t *);

#ifdef USE_MSQL
static int msql_connect(dbconn_t *, const char *, const char *, const char *, const char *);
static int msql_close(dbconn_t *);
static int msql_execute(dbconn_t *, const char *);
static array_t *msql_fetch(dbconn_t *, int);
static void msql_cleanup(dbconn_t *);
static char *msql_errormsg(dbconn_t *);

static db_defn_t msql = {"mSQL", msql_connect, msql_close,   msql_execute, msql_fetch,
                         NULL,   NULL,         msql_cleanup, NULL,         msql_errormsg};
#endif

#ifdef USE_MYSQL
static int MySQL_connect(dbconn_t *, const char *, const char *, const char *, const char *);
static int MySQL_close(dbconn_t *);
static int MySQL_execute(dbconn_t *, const char *);
static array_t *MySQL_fetch(dbconn_t *, int);
static void MySQL_cleanup(dbconn_t *);
static char *MySQL_errormsg(dbconn_t *);

static db_defn_t mysql = {"MySQL", MySQL_connect, MySQL_close,   MySQL_execute, MySQL_fetch,
                          nullptr, nullptr,       MySQL_cleanup, nullptr,       MySQL_errormsg};
#endif

#ifdef USE_POSTGRES
static int Postgres_connect(dbconn_t *, const char *, const char *, const char *, const char *);
static int Postgres_close(dbconn_t *);
static int Postgres_execute(dbconn_t *, const char *);
static array_t *Postgres_fetch(dbconn_t *, int);
static void Postgres_cleanup(dbconn_t *);
static char *Postgres_errormsg(dbconn_t *);

static db_defn_t postgres = {
    "Postgres", Postgres_connect, Postgres_close, Postgres_execute, Postgres_fetch, NULL,
    NULL,       Postgres_cleanup, NULL,           Postgres_errormsg};
#endif

#ifdef USE_SQLITE3
static int SQLite3_connect(dbconn_t *, const char *, const char *, const char *, const char *);
static int SQLite3_close(dbconn_t *);
static int SQLite3_execute(dbconn_t *, const char *);
static array_t *SQLite3_fetch(dbconn_t *, int);
static void SQLite3_cleanup(dbconn_t *);
static char *SQLite3_errormsg(dbconn_t *);

static db_defn_t SQLite3 = {
    "SQLite3", SQLite3_connect, SQLite3_close, SQLite3_execute, SQLite3_fetch, NULL,
    NULL,      SQLite3_cleanup, NULL,          SQLite3_errormsg};
#endif

static db_defn_t no_db = {"None",  nullptr, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr, nullptr};

/* valid_database
 *
 * Calls APPLY_VALID_DATABASE in the master object to provide some
 * security on which objects can tweak your database (we don't want
 * people doing "DELETE * FROM *" or equivalent for us)
 */
svalue_t *valid_database(const char *action, array_t *info) {
  svalue_t *ret;

  /*
   * Call valid_database(object ob, string action, mixed *info)
   *
   * Return: string - password for access
   *         int    - 1 for no password, accept, 0 deny
   */
  push_object(current_object);
  push_constant_string(action);
  push_refed_array(info);

  ret = apply_master_ob(APPLY_VALID_DATABASE, 3);
  if (ret && (ret == (svalue_t *)-1 ||
              (ret->type == T_STRING || (ret->type == T_NUMBER && ret->u.number)))) {
    return ret;
  }

  error("Database security violation attempted\n");
}

/* int db_close(int handle);
 *
 * Closes the connection to the database represented by the named handle
 *
 * Returns 1 on success, 0 on failure
 */
#ifdef F_DB_CLOSE
void f_db_close() {
  int ret = 0;
  db_t *db;

  valid_database("close", &the_null_array);

  db = find_db_conn(sp->u.number);
  if (!db) {
    error("Attempt to close an invalid database handle\n");
  }

  /* Cleanup any memory structures left around */
  if (db->type->cleanup) {
    db->type->cleanup(&(db->c));
  }

  if (db->type->close) {
    ret = db->type->close(&(db->c));
  }

  /* Remove the entry from the linked list */
  free_db_conn(db);

  sp->u.number = ret;
}
#endif

/* int db_commit(int handle);
 *
 * Commits the last set of transactions to the database
 * NOTE: MSQL does not have transaction logic, so since
 * MSQL is the only thing supported now, this does nothing
 * I have put it in, however, so people can write properly
 * portable LPC code
 *
 * Returns 1 on success, 0 on failure
 */
#ifdef F_DB_COMMIT
void f_db_commit() {
  int ret = 0;
  db_t *db;

  valid_database("commit", &the_null_array);

  db = find_db_conn(sp->u.number);
  if (!db) {
    error("Attempt to commit an invalid database handle\n");
  }

  if (db->type->commit) {
    ret = db->type->commit(&(db->c));
  }

  sp->u.number = ret;
}
#endif

/* int db_connect(string host, string database, string user, int type)
 *
 * Creates a database connection to the database named by the
 * second argument found on the host named by the first argument.
 * Note that this means you can connect to database servers running on
 * machines other than the one on which the mud is running.  It will
 * connect based on settings established at compile time for the
 * user id and password (if required).
 *
 * Returns a new database handle.
 */
#ifdef F_DB_CONNECT
void f_db_connect() {
  char *errormsg = nullptr;
  const char *user = "", *database, *host = nullptr;
  db_t *db;
  array_t *info;
  svalue_t *mret;
  int handle, ret = 0, args = 0, type;

#ifdef DEFAULT_DB
  type = DEFAULT_DB;
#else
  type = 0;
#endif

  switch (st_num_arg) {
    case 4:
      type = (sp - (args++))->u.number;
      // fallthrough
    case 3:
      user = (sp - (args++))->u.string;
      // fallthrough
    case 2:
      database = (sp - (args++))->u.string;
      // fallthrough
    case 1:
      host = (sp - (args++))->u.string;
      break;
    default:
      // unreachable
      break;
  }

  info = allocate_empty_array(3);
  info->item[0].type = info->item[1].type = info->item[2].type = T_STRING;
  info->item[0].subtype = info->item[1].subtype = info->item[2].subtype = STRING_MALLOC;
  info->item[0].u.string = string_copy(database, "f_db_connect:1");
  if (*host) {
    info->item[1].u.string = string_copy(host, "f_db_connect:2");
  } else {
    info->item[1] = const0;
  }
  info->item[2].u.string = string_copy(user, "f_db_connect:3");

  mret = valid_database("connect", info);

  handle = create_db_conn();
  if (!handle) {
    pop_n_elems(args);
    push_number(0);
    return;
  }
  db = find_db_conn(handle);

  switch (type) {
    default:
/* fallthrough */
#ifdef USE_MSQL
#if USE_MSQL - 0
    case USE_MSQL:
#endif
      db->type = &msql;
      break;
#endif
#ifdef USE_MYSQL
#if USE_MYSQL - 0
    case USE_MYSQL:
#endif
      db->type = &mysql;
      break;
#endif
#ifdef USE_SQLITE3
#if USE_SQLITE3 - 0
    case USE_SQLITE3:
#endif
      db->type = &SQLite3;
      break;
#endif
#ifdef USE_POSTGRES
#if USE_POSTGRES - 0
    case USE_POSTGRES:
#endif
      db->type = &postgres;
      break;
#endif
  }

  if (db->type->connect) {
    ret = db->type->connect(
        &(db->c), host, database, user,
        (mret != (svalue_t *)-1 && mret->type == T_STRING ? mret->u.string : nullptr));
  }

  pop_n_elems(args);

  if (!ret) {
    if (db->type->error) {
      errormsg = db->type->error(&(db->c));
      push_malloced_string(errormsg);
    } else {
      push_number(0);
    }
    free_db_conn(db);
  } else {
    push_number(handle);
  }
}
#endif

/* mixed db_exec(int handle, string sql)
 *
 * Executes the SQL statement passed for the named connection handle.
 * If data needs to be retrieved from this execution, it should be done
 * through db_fetch() after making the call to db_exec()
 *
 * Returns number of rows in result set on success, an error string on failure
 * NOTE: the number of rows on INSERT, UPDATE, and DELETE statements will
 * be zero since there is no result set.
 */
#ifdef PACKAGE_ASYNC
extern pthread_mutex_t *db_mut;
#endif
#ifdef F_DB_EXEC
void f_db_exec() {
  int ret = 0;
  db_t *db;
  array_t *info;
  info = allocate_empty_array(1);
  info->item[0].type = T_STRING;
  info->item[0].subtype = STRING_MALLOC;
  info->item[0].u.string = string_copy(sp->u.string, "f_db_exec");
  valid_database("exec", info);

  db = find_db_conn((sp - 1)->u.number);
  if (!db) {
    error("Attempt to exec on an invalid database handle\n");
  }

#ifdef PACKAGE_ASYNC
  pthread_mutex_lock(db_mut);
#endif
  if (db->type->cleanup) {
    db->type->cleanup(&(db->c));
  }

  if (db->type->execute) {
    ret = db->type->execute(&(db->c), sp->u.string);
  }

  pop_stack();
  if (ret == -1) {
    if (db->type->error) {
      char *errormsg;

      errormsg = db->type->error(&(db->c));
      put_malloced_string(errormsg);
    } else {
      put_constant_string("Unknown error");
    }
  } else {
    sp->u.number = ret;
  }
#ifdef PACKAGE_ASYNC
  pthread_mutex_unlock(db_mut);
#endif
}
#endif

/* array db_fetch(int db_handle, int row);
 *
 * Returns the result set from the last database transaction
 * performed through db_exec() on the db handle in question for the row
 * named.  For example, db_exec(10, "SELECT player_name from t_player") might
 * have returned two rows.  Typical code to extract that data might be:
 *     string *res;
 *     mixed rows;
 *     int dbconn, i;
 *
 *     dbconn = db_connect("nightmare.imaginary.com", "db_mud");
 *     if( dbconn < 1 ) return 0;
 *     rows = db_exec(dbconn, "SELECT player_name from t_player");
 *     if( !rows ) write("No rows returned.");
 *     else if( stringp(rows) ) write(rows);
 *     else for(i=1; i<=rows; i++) {
 *         res = db_fetch(dbconn, i);
 *         write(res[0]);
 *     }
 *     db_close(dbconn);
 *     return 1;
 *
 * Returns an array of columns from the named row on success.
 */
#ifdef F_DB_FETCH
void f_db_fetch() {
  db_t *db;
  array_t *ret;

  valid_database("fetch", &the_null_array);

  db = find_db_conn((sp - 1)->u.number);
  if (!db) {
    error("Attempt to fetch from an invalid database handle\n");
  }

  if (db->type->fetch) {
    ret = db->type->fetch(&(db->c), sp->u.number);
  } else {
    ret = &the_null_array;
  }

  pop_stack();
  if (!ret) {
    if (db->type->error) {
      char *errormsg;

      errormsg = db->type->error(&(db->c));
      put_malloced_string(errormsg);
    } else {
      sp->u.number = 0;
    }
  } else {
    put_array(ret);
  }
}
#endif

/* int db_rollback(int handle)
 *
 * Rollsback all db_exec() calls back to the last db_commit() call for the
 * named connection handle.
 * NOTE: MSQL does not support rollbacks
 *
 * Returns 1 on success, 0 on failure
 */
#ifdef F_DB_ROLLBACK
void f_db_rollback() {
  int ret = 0;
  db_t *db;

  valid_database("rollback", &the_null_array);

  db = find_db_conn(sp->u.number);
  if (!db) {
    error("Attempt to rollback an invalid database handle\n");
  }

  if (db->type->rollback) {
    ret = db->type->rollback(&(db->c));
  }

  if (ret > 0) {
    if (db->type->cleanup) {
      db->type->cleanup(&(db->c));
    }
  }

  sp->u.number = ret;
}
#endif

/* string db_status()
 *
 * Returns a string describing the database package's current status
 */
#ifdef F_DB_STATUS
void f_db_status() {
  int i;
  outbuffer_t out;

  outbuf_zero(&out);

  for (i = 0; i < db_conn_alloc; i++) {
    if (db_conn_list[i].flags & DB_FLAG_EMPTY) {
      continue;
    }

    outbuf_addv(&out, "Handle: %d (%s)\n", i + 1, db_conn_list[i].type->name);
    if (db_conn_list[i].type->status != nullptr) {
      db_conn_list[i].type->status(&(db_conn_list[i].c), &out);
    }
  }

  outbuf_push(&out);
}
#endif

void db_cleanup() {
  int i;

  for (i = 0; i < db_conn_alloc; i++) {
    if (!(db_conn_list[i].flags & DB_FLAG_EMPTY)) {
      if (db_conn_list[i].type->cleanup) {
        db_conn_list[i].type->cleanup(&(db_conn_list[i].c));
      }

      if (db_conn_list[i].type->close) {
        db_conn_list[i].type->close(&(db_conn_list[i].c));
      }

      db_conn_list[i].flags = DB_FLAG_EMPTY;
      db_conn_used--;
    }
  }
}

int create_db_conn() {
  int i;

#ifdef PACKAGE_ASYNC
  if (!db_mut) {
    db_mut = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(db_mut, nullptr);
  }
  pthread_mutex_lock(db_mut);
  DEFER { pthread_mutex_unlock(db_mut); };
#endif

  /* allocate more slots if we need them */
  if (db_conn_alloc == db_conn_used) {
    i = db_conn_alloc;
    db_conn_alloc += 10;
    if (!db_conn_list) {
      db_conn_list = (db_t *)DCALLOC(db_conn_alloc, sizeof(db_t), TAG_DB, "create_db_conn");
    } else {
      db_conn_list = RESIZE(db_conn_list, db_conn_alloc, db_t, TAG_DB, "create_db_conn");
    }
    while (i < db_conn_alloc) {
      db_conn_list[i++].flags = DB_FLAG_EMPTY;
    }
  }

  for (i = 0; i < db_conn_alloc; i++) {
    if (db_conn_list[i].flags & DB_FLAG_EMPTY) {
      db_conn_list[i].flags = 0;
      db_conn_list[i].type = &no_db;
      db_conn_used++;
      return i + 1;
    }
  }

  fatal("dbConnAlloc != dbConnUsed, but no empty slots");
}

db_t *find_db_conn(int handle) {
  if (handle < 1 || handle > db_conn_alloc || db_conn_list[handle - 1].flags & DB_FLAG_EMPTY) {
    return nullptr;
  }
  return &(db_conn_list[handle - 1]);
}

void free_db_conn(db_t *db) {
  DEBUG_CHECK(db->flags & DB_FLAG_EMPTY, "Freeing DB connection that is already freed\n");
  DEBUG_CHECK(!db_conn_used, "Freeing DB connection when dbConnUsed == 0\n");
  db_conn_used--;
  db->flags |= DB_FLAG_EMPTY;
}

#ifdef DEBUGMALLOC_EXTENSIONS
void mark_db_conn() {
  auto *node = db_conn_list;
  if (node) {
    DO_MARK(node, TAG_DB);
  }
}
#endif

/*
 * MySQL support
 */
#ifdef USE_MYSQL
static void MySQL_cleanup(dbconn_t *c) {
  *(c->mysql.errormsg) = 0;
  if (c->mysql.results) {
    mysql_free_result(c->mysql.results);
    c->mysql.results = nullptr;
  }
}

static char *MySQL_errormsg(dbconn_t *c) {
  if (*(c->mysql.errormsg)) {
    return string_copy(c->mysql.errormsg, "MySQL_errormsg:1");
  }

  return string_copy(mysql_error(c->mysql.handle), "MySQL_errormsg:2");
}

static int MySQL_close(dbconn_t *c) {
  mysql_close(c->mysql.handle);
  FREE(c->mysql.handle);
  c->mysql.handle = nullptr;

  return 1;
}

static int MySQL_execute(dbconn_t *c, const char *s) {
  if (!mysql_query(c->mysql.handle, s)) {
    c->mysql.results = mysql_store_result(c->mysql.handle);
    if (c->mysql.results) {
      return mysql_num_rows(c->mysql.results);
    }

    /* Queries returning no input can return a NULL handle */
    if (!mysql_errno(c->mysql.handle)) {
      return 0;
    }
  }

  return -1;
}

static array_t *MySQL_fetch(dbconn_t *c, int row) {
  array_t *v;
  MYSQL_ROW target_row;
  unsigned int i, num_fields;

  if (!c->mysql.results) {
    return &the_null_array;
  }
  if (row < 0 || row > mysql_num_rows(c->mysql.results)) {
    return &the_null_array;
  }

  num_fields = mysql_num_fields(c->mysql.results);
  if (num_fields < 1) {
    return &the_null_array;
  }

  if (row > 0) {
    mysql_data_seek(c->mysql.results, row - 1);
    target_row = mysql_fetch_row(c->mysql.results);
    if (!target_row) {
      return &the_null_array;
    }
  }

  v = allocate_empty_array(num_fields);
  for (i = 0; i < num_fields; i++) {
    MYSQL_FIELD *field;

    field = mysql_fetch_field(c->mysql.results);

    if (row == 0) {
      if (field == (MYSQL_FIELD *)nullptr) {
        v->item[i] = const0u;
      } else {
        v->item[i].type = T_STRING;
        v->item[i].subtype = STRING_MALLOC;
        v->item[i].u.string = string_copy(field->name, "f_db_fetch");
      }
      continue;
    }

    if (!field || !target_row[i]) {
      v->item[i] = const0u;
    } else {
      switch (field->type) {
        case FIELD_TYPE_TINY:
        case FIELD_TYPE_SHORT:
        case FIELD_TYPE_DECIMAL:
        case FIELD_TYPE_NEWDECIMAL:
        case FIELD_TYPE_LONG:
        case FIELD_TYPE_INT24:
        case FIELD_TYPE_LONGLONG:
          v->item[i].type = T_NUMBER;
          v->item[i].subtype = 0;
          v->item[i].u.number = strtoull(target_row[i], nullptr, 10);
          break;

        case FIELD_TYPE_FLOAT:
        case FIELD_TYPE_DOUBLE:
          v->item[i].type = T_REAL;
          v->item[i].u.real = strtod(target_row[i], nullptr);
          break;

        case FIELD_TYPE_TINY_BLOB:
        case FIELD_TYPE_MEDIUM_BLOB:
        case FIELD_TYPE_LONG_BLOB:
        case FIELD_TYPE_BLOB:
        case FIELD_TYPE_STRING:
        case FIELD_TYPE_VAR_STRING:
          if (field->flags & BINARY_FLAG) {
            v->item[i].type = T_BUFFER;
            v->item[i].u.buf = allocate_buffer(field->max_length);
            write_buffer(v->item[i].u.buf, 0, target_row[i], field->max_length);
          } else {
            v->item[i].type = T_STRING;
            if (target_row[i]) {
              v->item[i].subtype = STRING_MALLOC;
              v->item[i].u.string = string_copy(target_row[i], "MySQL_fetch");
            } else {
              v->item[i].subtype = STRING_CONSTANT;
              v->item[i].u.string = "";
            }
          }
          break;

        default:
          v->item[i] = const0u;
          break;
      }
    }
  }

  mysql_field_seek(c->mysql.results, 0);
  return v;
}

static int MySQL_connect(dbconn_t *c, const char *host, const char *database, const char *username,
                         const char *password) {
  int ret;
  MYSQL *tmp;

  tmp = (MYSQL *)DMALLOC(sizeof(MYSQL), TAG_DB, "MySQL_connect");
  tmp = mysql_init(tmp);
  *(c->mysql.errormsg) = 0;
  c->mysql.handle = mysql_real_connect(tmp, host, username, password, database, 0, nullptr, 0);
  // c->mysql.handle = mysql_connect(tmp, host, username, password);
  if (!c->mysql.handle) {
    strncpy(c->mysql.errormsg, mysql_error(tmp), sizeof(c->mysql.errormsg));
    c->mysql.errormsg[sizeof(c->mysql.errormsg) - 1] = 0;
    FREE(tmp);
    return 0;
  }

  ret = mysql_select_db(c->mysql.handle, database);
  if (ret) {
    strncpy(c->mysql.errormsg, mysql_error(c->mysql.handle), sizeof(c->mysql.errormsg));
    c->mysql.errormsg[sizeof(c->mysql.errormsg) - 1] = 0;
    mysql_close(c->mysql.handle);
    c->mysql.handle = nullptr;
    FREE(tmp);
    return 0;
  }

  c->mysql.results = nullptr;
  return 1;
}
#endif

/*
 * mSQL support
 */
#ifdef USE_MSQL
static void msql_cleanup(dbconn_t *c) {
  if (c->msql.result_set) {
    msqlFreeResult(c->msql.result_set);
    c->msql.result_set = 0;
  }
}

static int msql_close(dbconn_t *c) {
  msqlClose(c->msql.handle);
  c->msql.handle = -1;

  return 1;
}

static int msql_execute(dbconn_t *c, const char *s) {
  if (msqlQuery(c->msql.handle, s) != -1) {
    c->msql.result_set = msqlStoreResult();
    if (!c->msql.result_set) {
      /* Query was an UPDATE or INSERT or DELETE */
      return 0;
    }
    return msqlNumRows(c->msql.result_set);
  }

  return -1;
}

static array_t *msql_fetch(dbconn_t *c, int row) {
  int i, num_fields;
  m_row this_row;
  array_t *v;

  if (!c->msql.result_set) {
    return &the_null_array;
  }
  if (row < 1 || row > msqlNumRows(c->msql.result_set)) {
    return &the_null_array;
  }

  num_fields = msqlNumFields(c->msql.result_set);
  if (num_fields < 1) {
    return &the_null_array;
  }

  msqlDataSeek(c->msql.result_set, row - 1);
  this_row = msqlFetchRow(c->msql.result_set);
  if (!this_row) {
    return &the_null_array;
  }

  v = allocate_empty_array(num_fields);
  for (i = 0; i < num_fields; i++) {
    m_field *field;

    field = msqlFetchField(c->msql.result_set);
    if (!field || !this_row[i]) {
      v->item[i] = const0u;
    } else {
      switch (field->type) {
        case INT_TYPE:
        case UINT_TYPE:
          v->item[i].type = T_NUMBER;
          v->item[i].u.number = atoi(this_row[i]);
          break;

        case REAL_TYPE:
        case MONEY_TYPE:
          v->item[i].type = T_REAL;
          v->item[i].u.real = atof(this_row[i]);
          break;

        case CHAR_TYPE:
        case TEXT_TYPE:
        case DATE_TYPE:
        case TIME_TYPE:
          v->item[i].type = T_STRING;
          v->item[i].subtype = STRING_MALLOC;
          v->item[i].u.string = string_copy(this_row[i], "msql_fetch");
          break;

        default:
          v->item[i] = const0u;
          break;
      }
    }
  }

  msqlFieldSeek(c->msql.result_set, 0);
  return v;
}

static int msql_connect(dbconn_t *c, char *host, char *database, char *username, char *password) {
  c->msql.handle = msqlConnect(host);
  if (c->msql.handle < 1) {
    return 0;
  }

  if (msqlSelectDB(c->msql.handle, database) == -1) {
    msqlClose(c->msql.handle);
    return 0;
  }

  c->msql.result_set = 0;
  return 1;
}

static char *msql_errormsg(dbconn_t *c) { return string_copy(msqlErrMsg, "msql_errormsg"); }
#endif

/*
 * SQLite v3 support
 * ajandurah@demonslair (Mark Lyndoe)
 */
#ifdef USE_SQLITE3
static int SQLite3_connect(dbconn_t *c, const char *host, const char *database,
                           const char *username, const char *password) {
  const char *filename = check_valid_path(database, master_ob, "sqlite3_connect", 1);

  if (sqlite3_open(filename, &c->SQLite3.handle)) {
    strncpy(c->SQLite3.errormsg, sqlite3_errmsg(c->SQLite3.handle), sizeof(c->SQLite3.errormsg));
    c->SQLite3.errormsg[sizeof(c->SQLite3.errormsg) - 1] = 0;
    sqlite3_close(c->SQLite3.handle);
    return 0;
  }

  c->SQLite3.results = 0;
  c->SQLite3.nrows = 0;
  c->SQLite3.last_row = 0;
  c->SQLite3.step_res = 0;
  return 1;
}

static int SQLite3_close(dbconn_t *c) {
  if (c->SQLite3.results) {
    sqlite3_finalize(c->SQLite3.results);
  }

  sqlite3_close(c->SQLite3.handle);
  c->SQLite3.handle = 0;
  c->SQLite3.nrows = 0;
  c->SQLite3.last_row = 0;
  c->SQLite3.step_res = 0;
  return 1;
}

static void SQLite3_cleanup(dbconn_t *c) {
  if (c->SQLite3.results) {
    sqlite3_finalize(c->SQLite3.results);
    c->SQLite3.results = 0;
    c->SQLite3.last_row = 0;
    c->SQLite3.step_res = 0;
  }
}

static int SQLite3_execute(dbconn_t *c, const char *s) {
  char **result;

  if (sqlite3_prepare(c->SQLite3.handle, s, -1, &c->SQLite3.results, 0) != SQLITE_OK) {
    strncpy(c->SQLite3.errormsg, sqlite3_errmsg(c->SQLite3.handle), sizeof(c->SQLite3.errormsg));
    c->SQLite3.errormsg[sizeof(c->SQLite3.errormsg) - 1] = 0;
    return 0;
  }

  /* There has to be a better way of determining the number of rows in the
   * result
   * set. sqlite3_prepare() does not provide them, since we need to call
   * sqlite3_step()
   * to walk through them, which we dont want to do until db_fetch() is called.
   * This
   * hack means we actually have to execute the full SQL statement to get the
   * row
   * total.. expensive time wise unfortunately.
   */
  if (c->SQLite3.results) {
    if (sqlite3_get_table(c->SQLite3.handle, s, &result, &c->SQLite3.nrows, &c->SQLite3.ncolumns,
                          NULL) != SQLITE_OK) {
      sqlite3_free_table(result);
      sqlite3_finalize(c->SQLite3.results);
      return 0;
    }

    sqlite3_free_table(result);
    c->SQLite3.last_row = 0;
    c->SQLite3.step_res = 0;
    return c->SQLite3.nrows;
  }

  return -1;
}

static array_t *SQLite3_fetch(dbconn_t *c, int row) {
  int cols, last_row, length, i;
  array_t *v;

  if (!c->SQLite3.results) {
    return &the_null_array;
  }

  if (c->SQLite3.step_res && c->SQLite3.step_res != SQLITE_ROW) {
    return &the_null_array;
  }

  if (row < 0 || row > c->SQLite3.nrows) {
    return &the_null_array;
  }

  cols = sqlite3_column_count(c->SQLite3.results);
  if (cols < 1) {
    return &the_null_array;
  }

  /* If the fetch is for row 0 then we don't return a row from the prepared
   * statement, instead we return an array containing the column names. This
   * ability is useful in a number of circumstances when the column names are
   * not known in advance for a query. We do not step the statement nor do we
   * adjust the last_row index since sqlite provides a method to obtain the
   * column names at any time.
   */
  if (row == 0) {
    v = allocate_empty_array(cols);
    for (i = 0; i < cols; i++) {
      v->item[i].type = T_STRING;
      v->item[i].subtype = STRING_MALLOC;
      v->item[i].u.string =
          string_copy((char *)sqlite3_column_name(c->SQLite3.results, i), "SQLite3_fetch");
    }

    return v;
  }

  /* There is no quick entry to a row in the prepared statement. Thus we have
   * too loop through till we reach the desired row, but only if the last row
   * that we fetched is not the previous row... confused? join the club.
   */
  last_row = c->SQLite3.last_row;

  /* If the requested row is before the last row that was accessed then we
   * reset the compiled statement before continuing. This is because we can't
   * just select a row at will using sqlite and we can't rewind a step either.
   * We could just reset the result statement at the end of this function like
   * the msql and mysql versions, but that gets expensive next time it's called
   * if we need to walk through everything again... sigh
   */
  if (row < last_row) {
    sqlite3_reset(c->SQLite3.results);
    c->SQLite3.last_row = 0;
    c->SQLite3.step_res = 0;
    last_row = 0;
  }

  /* If the requested row is the same as the last one, ie: it's been requested
   * again! we do not need to step forward, so we miss the row location loop
   * and get straight to the nitty gritty of building the result array. If not
   * we loop through from the last_row requested to the one requested this time
   * using sqlite3_step(). As long as the result is SQLITE_ROW we move on, if
   * not then either an error occured or there are no more rows so we return a
   * null array. The result is stored in the sqlite structure for later checks
   * so if fetch is called again on a completed or errornous statement we can
   * fail out sooner saving time.
   */
  if ((row != last_row) && (last_row < row)) {
    for (i = last_row; i < row; i++) {
      c->SQLite3.step_res = sqlite3_step(c->SQLite3.results);

      if (c->SQLite3.step_res == SQLITE_ROW) {
        break;
      } else {
        return &the_null_array;
      }
    }
  }

  v = allocate_empty_array(cols);
  for (i = 0; i < cols; i++) {
    switch (sqlite3_column_type(c->SQLite3.results, i)) {
      case SQLITE_INTEGER:
        v->item[i].type = T_NUMBER;
        v->item[i].u.number = sqlite3_column_int64(c->SQLite3.results, i);
        break;

      case SQLITE_FLOAT:
        v->item[i].type = T_REAL;
        v->item[i].u.real = (double)sqlite3_column_double(c->SQLite3.results, i);
        break;

      case SQLITE3_TEXT:
        v->item[i].type = T_STRING;
        v->item[i].subtype = STRING_MALLOC;
        v->item[i].u.string =
            string_copy((char *)sqlite3_column_text(c->SQLite3.results, i), "SQLite3_fetch");
        break;

      case SQLITE_BLOB:
        length = sqlite3_column_bytes(c->SQLite3.results, i);
        v->item[i].type = T_BUFFER;
        v->item[i].u.buf = allocate_buffer(length);
        write_buffer(v->item[i].u.buf, 0, (char *)sqlite3_column_blob(c->SQLite3.results, i),
                     length);
        break;

      default:
        v->item[i] = const0u;
        break;
    }
  }

  c->SQLite3.last_row = row;
  return v;
}

static char *SQLite3_errormsg(dbconn_t *c) {
  if (*(c->SQLite3.errormsg)) {
    return string_copy((char *)c->SQLite3.errormsg, "SQLite3_errormsg:1");
  }

  return string_copy((char *)sqlite3_errmsg(c->SQLite3.handle), "SQLite3_errormsg:2");
}
#endif
/*
 * Postgres support
 */
#ifdef USE_POSTGRES
static void Postgres_cleanup(dbconn_t *c) { c->postgres.res = 0; }

static char *Postgres_errormsg(dbconn_t *c) {
  return string_copy(PQerrorMessage(c->postgres.conn), "postgresql_errormsg");
}

static int Postgres_close(dbconn_t *c) {
  PQclear(c->postgres.res);
  PQfinish(c->postgres.conn);
  return 1;
}

static int Postgres_execute(dbconn_t *c, const char *s) {
  c->postgres.res = PQexec(c->postgres.conn, s);

  if ((PQresultStatus(c->postgres.res)) == PGRES_TUPLES_OK) {
    return PQntuples(c->postgres.res);
  }

  if (PQresultStatus(c->postgres.res) == PGRES_COMMAND_OK) {
    return 0;
  }

  debug_message("FT: Query failed: \"%s\"\n", PQresultErrorMessage(c->postgres.res));
  return -1;
}

static int Postgres_connect(dbconn_t *c, const char *host, const char *database,
                            const char *username, const char *password) {
  int buffsize;

  const char *connstr = "host = '%s' dbname = '%s' user = '%s' password = '%s'";
  buffsize =
      strlen(connstr) + strlen(host) + strlen(database) + strlen(username) + strlen(password);
  char *conninfo = (char *)malloc(buffsize);
  if (conninfo != NULL) {
    sprintf(conninfo, connstr, host, database, username, password);
  }

  c->postgres.conn = PQconnectdb(conninfo);
  free(conninfo);

  if ((PQstatus(c->postgres.conn) != CONNECTION_OK)) {
    return 0;
  }
  return 1;
}

static array_t *Postgres_fetch(dbconn_t *c, int row) {
  array_t *v;
  unsigned int i, num_fields;

  if (!c->postgres.res) {
    return &the_null_array;
  }

  num_fields = PQnfields(c->postgres.res);

  if (row < -1 || row > PQntuples(c->postgres.res)) {
    return &the_null_array;
  }

  if (num_fields < 1) {
    return &the_null_array;
  }

  if (row == -1) {
    v = allocate_empty_array(num_fields);
    for (i = 0; i < num_fields; i++) {
      v->item[i].type = T_STRING;
      v->item[i].subtype = STRING_MALLOC;
      v->item[i].u.string = string_copy(PQfname(c->postgres.res, i), "f_db_fetch");
    }
  }

  if (row >= 0) {
    v = allocate_empty_array(num_fields);
    for (i = 0; i < num_fields; i++) {
      if (PQgetisnull(c->postgres.res, row, i)) {
        v->item[i] = const0u;
      } else {
        v->item[i].type = T_STRING;
        v->item[i].subtype = STRING_MALLOC;
        v->item[i].u.string = string_copy(PQgetvalue(c->postgres.res, row, i), "postgres_fetch");
      }
    }
  }
  return v;
}
#endif
