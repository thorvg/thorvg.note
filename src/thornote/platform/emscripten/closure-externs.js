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

/** @record */
function ThorNoteFSPath() {}
ThorNoteFSPath.prototype.exists;

/** @record */
function ThorNoteFSOptions() {}
ThorNoteFSOptions.prototype.canOwn;
