#pragma once
#include <QString>
namespace litecode::core {
enum class GitPullPolicy { Repository, Merge, Rebase, FastForwardOnly };
struct GitPreferences {
    QString branchPrefix;
    QString branchWhitespace{"-"};
    bool randomBranchName{};
    bool pruneOnFetch{};
    bool fetchOnPull{};
    bool pullTags{true};
    bool autoStash{};
    bool allowNoVerifyCommit{};
    bool allowForcePush{};
    bool useForcePushWithLease{true};
    bool useForcePushIfIncludes{true};
    bool confirmForcePush{true};
    GitPullPolicy pullPolicy{GitPullPolicy::Repository};
};
} // namespace litecode::core
