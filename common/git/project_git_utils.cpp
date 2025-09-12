/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "project_git_utils.h"
#include "git_backend.h"

#include <wx/string.h>
#include <string_utils.h>

namespace KIGIT
{

git_repository* PROJECT_GIT_UTILS::GetRepositoryForFile( const char* aFilename )
{
    return GetGitBackend()->GetRepositoryForFile( aFilename );
}

int PROJECT_GIT_UTILS::CreateBranch( git_repository* aRepo, const wxString& aBranchName )
{
    return GetGitBackend()->CreateBranch( aRepo, aBranchName );
}

bool PROJECT_GIT_UTILS::RemoveVCS( git_repository*& aRepo, const wxString& aProjectPath,
                                  bool aRemoveGitDir, wxString* aErrors )
{
    return GetGitBackend()->RemoveVCS( aRepo, aProjectPath, aRemoveGitDir, aErrors );
}

wxString PROJECT_GIT_UTILS::GetCurrentHash( const wxString& aProjectFile, bool aShort )
{
    wxString result = wxT( "no hash" );
    git_repository* repo = PROJECT_GIT_UTILS::GetRepositoryForFile( TO_UTF8( aProjectFile ) );

    if( repo )
    {
        git_reference* head = nullptr;

    if( git_repository_head( &head, repo ) == 0 )
        {
            const git_oid* oid = git_reference_target( head );

            if( oid )
            {
                char buf[41];
                size_t len = aShort ? 8 : 41;
                git_oid_tostr( buf, len, oid );
                result = wxString::FromUTF8( buf );
            }

            git_reference_free( head );
        }

        git_repository_free( repo );
    }

    return result;
}

wxString PROJECT_GIT_UTILS::GetLatestTag( const wxString& aProjectFile )
{
    wxString result = wxEmptyString;
    git_repository* repo = PROJECT_GIT_UTILS::GetRepositoryForFile( TO_UTF8( aProjectFile ) );

    if( repo )
    {
        git_strarray tag_names;
        if( git_tag_list( &tag_names, repo ) == 0 )
        {
            git_time_t latest_time = 0;
            wxString latest_tag;

            // Find the most recent tag by comparing commit times
            for( size_t i = 0; i < tag_names.count; ++i )
            {
                git_reference* tag_ref = nullptr;
                wxString tag_name = wxString::FromUTF8( tag_names.strings[i] );
                wxString ref_name = wxString::Format( wxT( "refs/tags/%s" ), tag_name );

                if( git_reference_lookup( &tag_ref, repo, TO_UTF8( ref_name ) ) == 0 )
                {
                    const git_oid* tag_oid = git_reference_target( tag_ref );
                    if( tag_oid )
                    {
                        git_object* obj = nullptr;
                        if( git_object_lookup( &obj, repo, tag_oid, GIT_OBJECT_ANY ) == 0 )
                        {
                            git_commit* commit = nullptr;
                            git_time_t commit_time = 0;

                            if( git_object_type( obj ) == GIT_OBJECT_TAG )
                            {
                                git_tag* tag = (git_tag*)obj;
                                const git_oid* target_oid = git_tag_target_id( tag );
                                git_object* target = nullptr;
                                if( git_object_lookup( &target, repo, target_oid, GIT_OBJECT_COMMIT ) == 0 )
                                {
                                    commit = (git_commit*)target;
                                }
                            }
                            else if( git_object_type( obj ) == GIT_OBJECT_COMMIT )
                            {
                                commit = (git_commit*)obj;
                            }

                            if( commit )
                            {
                                commit_time = git_commit_time( commit );
                                if( commit_time > latest_time )
                                {
                                    latest_time = commit_time;
                                    latest_tag = tag_name;
                                }
                                if( git_object_type( obj ) == GIT_OBJECT_TAG )
                                    git_object_free( (git_object*)commit );
                            }

                            git_object_free( obj );
                        }
                    }
                    git_reference_free( tag_ref );
                }
            }

            result = latest_tag;
            git_strarray_dispose( &tag_names );
        }

        git_repository_free( repo );
    }

    return result;
}

int PROJECT_GIT_UTILS::GetCommitsSinceLatestTag( const wxString& aProjectFile )
{
    int count = 0;
    git_repository* repo = PROJECT_GIT_UTILS::GetRepositoryForFile( TO_UTF8( aProjectFile ) );

    if( repo )
    {
        wxString latest_tag = GetLatestTag( aProjectFile );
        if( !latest_tag.IsEmpty() )
        {
            git_reference* head_ref = nullptr;
            git_reference* tag_ref = nullptr;

            if( git_repository_head( &head_ref, repo ) == 0 )
            {
                wxString tag_ref_name = wxString::Format( wxT( "refs/tags/%s" ), latest_tag );
                if( git_reference_lookup( &tag_ref, repo, TO_UTF8( tag_ref_name ) ) == 0 )
                {
                    const git_oid* head_oid = git_reference_target( head_ref );
                    const git_oid* tag_oid = git_reference_target( tag_ref );

                    if( head_oid && tag_oid )
                    {
                        git_revwalk* walker = nullptr;
                        if( git_revwalk_new( &walker, repo ) == 0 )
                        {
                            git_revwalk_push( walker, head_oid );

                            // Find the tag commit
                            git_object* tag_obj = nullptr;
                            git_oid target_oid;
                            if( git_object_lookup( &tag_obj, repo, tag_oid, GIT_OBJECT_ANY ) == 0 )
                            {
                                if( git_object_type( tag_obj ) == GIT_OBJECT_TAG )
                                {
                                    git_tag* tag = (git_tag*)tag_obj;
                                    git_oid_cpy( &target_oid, git_tag_target_id( tag ) );
                                }
                                else
                                {
                                    git_oid_cpy( &target_oid, tag_oid );
                                }
                                git_object_free( tag_obj );

                                git_oid commit_oid;
                                while( git_revwalk_next( &commit_oid, walker ) == 0 )
                                {
                                    if( git_oid_equal( &commit_oid, &target_oid ) )
                                        break;
                                    count++;
                                }
                            }

                            git_revwalk_free( walker );
                        }
                    }

                    git_reference_free( tag_ref );
                }
                git_reference_free( head_ref );
            }
        }

        git_repository_free( repo );
    }

    return count;
}

wxString PROJECT_GIT_UTILS::GetGitRevision( const wxString& aProjectFile )
{
    wxString latest_tag = GetLatestTag( aProjectFile );
    if( latest_tag.IsEmpty() )
        return wxEmptyString;

    int commits_since = GetCommitsSinceLatestTag( aProjectFile );
    if( commits_since == 0 )
        return latest_tag;
    else
        return wxString::Format( wxT( "%s-%d" ), latest_tag, commits_since );
}

} // namespace KIGIT
