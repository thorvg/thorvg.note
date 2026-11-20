/** @externs */

/** @record */
function ThorNoteFS() {}
ThorNoteFS.prototype.mkdir;
ThorNoteFS.prototype.writeFile;
ThorNoteFS.prototype.readFile;
ThorNoteFS.prototype.mkdirTree;
ThorNoteFS.prototype.mount;
ThorNoteFS.prototype.syncfs;
ThorNoteFS.prototype.symlink;
ThorNoteFS.prototype.analyzePath;
ThorNoteFS.prototype.unlink;
ThorNoteFS.prototype.rename;
ThorNoteFS.prototype.stat;
ThorNoteFS.prototype.utime;

// MEMFS also accesses these attributes by their string names in setattr.
/** @record */
function ThorNoteFileStat() {}
ThorNoteFileStat.prototype.atime;
ThorNoteFileStat.prototype.mtime;
ThorNoteFileStat.prototype.ctime;

/** @record */
function ThorNoteFSPath() {}
ThorNoteFSPath.prototype.exists;

/** @record */
function ThorNoteFSOptions() {}
ThorNoteFSOptions.prototype.canOwn;
