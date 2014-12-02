/* This file is part of RTags.

RTags is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTags is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#include "FileManager.h"
#include "Server.h"
#include "Filter.h"
#include "Project.h"

class ScanThread : public Thread
{
public:
    ScanThread(const std::shared_ptr<FileManager> &fileManager, const Path &path)
        : Thread(), mPath(path), mFilters(Server::instance()->options().excludeFilters), mFileManager(fileManager)
    {
        setAutoDelete(true);
    }
    ScanThread(const Path &path);
    virtual void run()
    {
        const Set<Path> p = paths(mPath, mFilters);
        std::weak_ptr<FileManager> weak = mFileManager;
        EventLoop::mainEventLoop()->callLater([weak, p]() {
                if (std::shared_ptr<FileManager> proj = weak.lock())
                    proj->onRecurseJobFinished(p);
            });
    }
    static Set<Path> paths(const Path &path, const List<String> &filters)
    {
        UserData userData = { Set<Path>(), filters };
        path.visit([](const Path &path, void *userData) {
                UserData *u = reinterpret_cast<UserData*>(userData);
                const Filter::Result result = Filter::filter(path, u->filters);
                switch (result) {
                case Filter::Filtered:
                    break;
                case Filter::Directory:
                    if (Path::exists(path + "/.rtags-ignore"))
                        return Path::Continue;
                    return Path::Recurse;
                case Filter::File:
                case Filter::Source:
                    u->paths.insert(path);
                    break;
                }
                return Path::Continue;
            }, &userData);

        return userData.paths;
    }
private:
    const Path mPath;
    const List<String> &mFilters;
    const std::weak_ptr<FileManager> mFileManager;
    struct UserData {
        Set<Path> paths;
        const List<String> &filters;
    };
};

FileManager::FileManager()
    : mLastReloadTime(0)
{
    mWatcher.added().connect(std::bind(&FileManager::onFileAdded, this, std::placeholders::_1));
    mWatcher.removed().connect(std::bind(&FileManager::onFileRemoved, this, std::placeholders::_1));
}

void FileManager::init(const std::shared_ptr<Project> &proj, Mode mode)
{
    mProject = proj;
    reload(mode);
}

void FileManager::reload(Mode mode)
{
    if (!Server::instance()->options().tests.isEmpty())
        mode = Synchronous;

    mLastReloadTime = Rct::monoMs();
    std::shared_ptr<Project> project = mProject.lock();
    assert(project);
    if (mode == Asynchronous) {
        startScanThread();
    } else {
        const Set<Path> paths = ScanThread::paths(project->path(),
                                                  Server::instance()->options().excludeFilters);
        onRecurseJobFinished(paths);
    }
}

void FileManager::onRecurseJobFinished(const Set<Path> &paths)
{
    std::lock_guard<std::mutex> lock(mMutex); // ### is this needed now?

    std::shared_ptr<Project> project = mProject.lock();
    assert(project);
    std::shared_ptr<FilesMap> map = project->files();
    assert(map);
    map->clear();
    mWatcher.clear();
    for (Set<Path>::const_iterator it = paths.begin(); it != paths.end(); ++it) {
        const Path parent = it->parentDir();
        if (parent.isEmpty()) {
            error() << "Got empty parent here" << *it;
            continue;
        }
        assert(!parent.isEmpty());
        Set<String> &dir = (*map)[parent];
        watch(parent);
        dir.insert(it->fileName());
    }
    assert(!map->contains(""));
}

void FileManager::onFileAdded(const Path &path)
{
    // error() << "File added" << path;
    std::lock_guard<std::mutex> lock(mMutex);
    if (path.isEmpty()) {
        return;
    }
    const Filter::Result res = Filter::filter(path);
    switch (res) {
    case Filter::Directory:
        watch(path);
        reload(Asynchronous);
        return;
    case Filter::Filtered:
        return;
    default:
        break;
    }

    std::shared_ptr<Project> project = mProject.lock();
    assert(project);
    const std::shared_ptr<FilesMap> map = project->files();
    const Path parent = path.parentDir();
    if (!parent.isEmpty()) {
        Set<String> &dir = (*map)[parent];
        watch(parent);
        dir.insert(path.fileName());
    } else {
        error() << "Got empty parent here" << path;
        reload(Asynchronous);
    }
    assert(!map->contains(Path()));
}

void FileManager::onFileRemoved(const Path &path)
{
    // error() << "File removed" << path;
    std::lock_guard<std::mutex> lock(mMutex);
    std::shared_ptr<Project> project = mProject.lock();
    const std::shared_ptr<FilesMap> map = project->files();
    if (map->contains(path)) {
        reload(Asynchronous);
        return;
    }
    const Path parent = path.parentDir();
    if (map->contains(parent)) {
        Set<String> &dir = (*map)[parent];
        dir.remove(path.fileName());
        if (dir.isEmpty()) {
            mWatcher.unwatch(parent);
            map->remove(parent);
        }
    }
}

static inline bool startsWith(const Path &left, const Path &right)
{
    assert(!left.isEmpty());
    return !right.isEmpty() && left.startsWith(right);
}

bool FileManager::contains(const Path &path) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::shared_ptr<Project> proj = mProject.lock();
    if (!proj)
        return false;
    if (startsWith(path, proj->path()))
        return true;
    const Path p = Path::resolved(path);
    if (p != path && startsWith(path, proj->path()))
        return true;
    return false;
}

void FileManager::watch(const Path &path)
{
    if (!(Server::instance()->options().options & Server::NoFileManagerWatch)
        && !path.contains("/.git/") && !path.contains("/.svn/") && !path.contains("/.cvs/")) {
        mWatcher.watch(path);
    }
}
void FileManager::startScanThread()
{
    std::shared_ptr<Project> project = mProject.lock();
    assert(project);
    ScanThread *thread = new ScanThread(shared_from_this(), project->path());
    thread->start();
}
