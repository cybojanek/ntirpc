/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_types.h"
#include "posixdb_internal.h"
#include "string.h"

fsal_posixdb_status_t fsal_posixdb_getInfoFromName( fsal_posixdb_conn * p_conn, /* IN */
                                                    fsal_handle_t * p_parent_directory_handle,  /* IN/OUT */
                                                    fsal_name_t * p_objectname, /* IN */
                                                    fsal_path_t * p_path,       /* OUT */
                                                    fsal_handle_t * p_handle /* OUT */  )
{
    fsal_posixdb_status_t st;
    char           query[2048];
    result_handle_t res;
    MYSQL_ROW      row;

    /* sanity check */
    if ( !p_conn || !p_handle )
    {
        ReturnCode( ERR_FSAL_POSIXDB_FAULT, 0 );
    }

#ifdef _DEBUG_FSAL
    printf( "object_name='%s'\n", p_objectname->name ?  p_objectname->name : "/" );
#endif


    BeginTransaction( p_conn );
    /* lookup for the handle of the file */
    if ( p_parent_directory_handle && p_parent_directory_handle->id )
    {
        snprintf( query, 2048,
                  "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, "
                  "Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
                  "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handlets "
                  "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s'",
                  p_parent_directory_handle->id,
                  p_parent_directory_handle->ts, p_objectname->name );

        st = db_exec_sql( p_conn, query, &res );
        if ( FSAL_POSIXDB_IS_ERROR( st ) )
            goto rollback;
    }
    else
    {
        /* get root handle: */

        st = db_exec_sql( p_conn,
                          "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
                          "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handlets "
                          "WHERE Parent.handleidparent=Parent.handleid AND Parent.handletsparent=Parent.handlets",
                          &res );
        if ( FSAL_POSIXDB_IS_ERROR( st ) )
            goto rollback;
    }
    /* res contains : Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype  */

    /* entry not found */
    if ( ( mysql_num_rows( res ) != 1 ) || ( ( row = mysql_fetch_row( res ) ) == NULL ) )
    {
        mysql_free_result( res );
        RollbackTransaction( p_conn );
        ReturnCode( ERR_FSAL_POSIXDB_NOENT, 0 );
    }

    p_handle->id = atoll( row[0] );
    p_handle->ts = atoi( row[1] );
    posixdb_internal_fillFileinfoFromStrValues( &( p_handle->info ), row[2], row[3],    /* devid, inode */
                                                row[4], /* nlink */
                                                row[5], /* ctime */
                                                row[6] );       /* ftype */
    mysql_free_result( res );

    /* Build the path of the object */
    if ( p_path && p_objectname )
    {
        /* build the path of the Parent */
        st = fsal_posixdb_buildOnePath( p_conn, p_parent_directory_handle, p_path );
        if ( FSAL_POSIXDB_IS_ERROR( st ) )
            goto rollback;

        /* then concatenate the filename */
        if ( !( p_path->len + 1 + p_objectname->len < FSAL_MAX_PATH_LEN ) )
        {
            RollbackTransaction( p_conn );
            ReturnCode( ERR_FSAL_POSIXDB_PATHTOOLONG, 0 );
        }
        p_path->path[p_path->len] = '/';
        strcpy( &p_path->path[p_path->len + 1], p_objectname->name );
        p_path->len += 1 + p_objectname->len;

        /* add the the path to cache */
        fsal_posixdb_CachePath( p_handle, p_path );
    }
    else
    {
        /* update handle if it was in cache */
        fsal_posixdb_UpdateInodeCache( p_handle );
    }

    return EndTransaction( p_conn );

  rollback:
    RollbackTransaction( p_conn );
    return st;
}






fsal_posixdb_status_t fsal_posixdb_getInfoFromHandle( fsal_posixdb_conn * p_conn,       /* IN */
                                                      fsal_handle_t * p_object_handle,  /* IN/OUT */
                                                      fsal_path_t * p_paths,    /* OUT */
                                                      int paths_size,   /* IN */
                                                      int *p_count /* OUT */  )
{
    fsal_posixdb_status_t st;
    result_handle_t res;
    MYSQL_ROW      row;
    fsal_handle_t  parent_directory_handle;
    int            i_path;
    int            toomanypaths = 0;
    char           query[2048];

    /* sanity check */
    if ( !p_conn || !p_object_handle || ( ( !p_paths || !p_count ) && paths_size > 0 ) )
    {
        ReturnCode( ERR_FSAL_POSIXDB_FAULT, 0 );
    }

#ifdef _DEBUG_FSAL
    printf( "OBJECT_ID=%lli\n", p_object_handle->id );
#endif

    BeginTransaction( p_conn );

    /* lookup for the handle of the file */

    if ( !fsal_posixdb_GetInodeCache( p_object_handle ) )
    {

        snprintf( query, 2048,
                  "SELECT Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
                  "FROM Handle WHERE handleid=%llu AND handlets=%u", p_object_handle->id,
                  p_object_handle->ts );

        st = db_exec_sql( p_conn, query, &res );
        if ( FSAL_POSIXDB_IS_ERROR( st ) )
            goto rollback;

        /* p_res contains : Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype  */

#ifdef _DEBUG_FSAL
        DisplayLog( "lookupHandle(%llu,%u)", p_object_handle->id,
                    ( unsigned int ) p_object_handle->ts );
#endif
        if ( ( mysql_num_rows( res ) != 1 ) || ( ( row = mysql_fetch_row( res ) ) == NULL ) )
        {
#ifdef _DEBUG_FSAL
            DisplayLog( "lookupHandle=%d entries", mysql_num_rows( res ) );
#endif
            mysql_free_result( res );
            RollbackTransaction( p_conn );
            ReturnCode( ERR_FSAL_POSIXDB_NOENT, 0 );
        }


        posixdb_internal_fillFileinfoFromStrValues( &( p_object_handle->info ),
                                                    row[0], row[1], row[2], row[3], row[4] );
        mysql_free_result( res );

        /* update the inode */
        fsal_posixdb_UpdateInodeCache( p_object_handle );
    }

    /* Build the paths of the object */
    if ( p_paths )
    {
        /* find all the paths to the object */

        snprintf( query, 2048, "SELECT name, handleidparent, handletsparent "
                  "FROM Parent WHERE handleid=%llu AND handlets=%u",
                  p_object_handle->id, p_object_handle->ts );

        st = db_exec_sql( p_conn, query, &res );
        if ( FSAL_POSIXDB_IS_ERROR( st ) )
            goto rollback;

        /* res contains name, handleidparent, handletsparent */
        *p_count = mysql_num_rows( res );
        if ( *p_count == 0 )
        {
            mysql_free_result( res );
            RollbackTransaction( p_conn );
            ReturnCode( ERR_FSAL_POSIXDB_NOPATH, 0 );
        }
        else if ( *p_count > paths_size )
        {
            toomanypaths = 1;

            DisplayLog( "Too many paths found for object %llu.%u: found=%u, max=%d",
                        p_object_handle->id, p_object_handle->ts, *p_count, paths_size );

            *p_count = paths_size;
        }

        for ( i_path = 0; i_path < *p_count; i_path++ )
        {
            unsigned int   tmp_len;

            row = mysql_fetch_row( res );
            if ( row == NULL )
            {
                mysql_free_result( res );
                RollbackTransaction( p_conn );
                ReturnCode( ERR_FSAL_POSIXDB_FAULT, 0 );
            }

            /* build the path of the parent directory */
            parent_directory_handle.id = atoll( row[1] );
            parent_directory_handle.ts = atoi( row[2] );

            st = fsal_posixdb_buildOnePath( p_conn, &parent_directory_handle, &p_paths[i_path] );
            if ( FSAL_POSIXDB_IS_ERROR( st ) )
                goto free_res;

            tmp_len = p_paths[i_path].len;

            if ( ( tmp_len > 0 ) && ( p_paths[i_path].path[tmp_len - 1] == '/' ) )
            {
                /* then concatenate the name of the file */
                /* but not concatenate '/' */
                if ( ( tmp_len + strlen( row[0] ) >= FSAL_MAX_PATH_LEN ) )
                {
                    mysql_free_result( res );
                    RollbackTransaction( p_conn );
                    ReturnCode( ERR_FSAL_POSIXDB_PATHTOOLONG, 0 );
                }
                strcpy( &p_paths[i_path].path[tmp_len], row[0] );
                p_paths[i_path].len += strlen( row[0] );

            }
            else
            {
                /* then concatenate the name of the file */
                if ( ( tmp_len + 1 + strlen( row[0] ) >= FSAL_MAX_PATH_LEN ) )
                {
                    mysql_free_result( res );
                    RollbackTransaction( p_conn );
                    ReturnCode( ERR_FSAL_POSIXDB_PATHTOOLONG, 0 );
                }
                p_paths[i_path].path[tmp_len] = '/';
                strcpy( &p_paths[i_path].path[tmp_len + 1], row[0] );
                p_paths[i_path].len += 1 + strlen( row[0] );
            }

            /* insert the object into cache */
            fsal_posixdb_CachePath( p_object_handle, &p_paths[i_path] );

        }

        mysql_free_result( res );
    }

    st = EndTransaction( p_conn );

    if ( toomanypaths )
        ReturnCode( ERR_FSAL_POSIXDB_TOOMANYPATHS, 0 );
    else
        return st;

  free_res:
    mysql_free_result( res );
  rollback:
    RollbackTransaction( p_conn );
    return st;
}


fsal_posixdb_status_t fsal_posixdb_getParentDirHandle( fsal_posixdb_conn * p_conn,      /* IN */
                                                       fsal_handle_t * p_object_handle, /* IN */
                                                       fsal_handle_t *
                                                       p_parent_directory_handle /* OUT */  )
{
    fsal_posixdb_status_t st;
    MYSQL_ROW      row;
    result_handle_t res;
    char           query[2048];

    /* sanity check */
    if ( !p_conn || !p_parent_directory_handle || !p_object_handle )
        ReturnCode( ERR_FSAL_POSIXDB_FAULT, 0 );


    /* no need to start a transaction, there is only one query */

    snprintf( query, 2048, "SELECT Parent.name, Parent.handleidparent, Parent.handletsparent, "
              "Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
              "FROM Parent LEFT JOIN Handle ON Parent.handleidparent = Handle.handleid AND Parent.handletsparent=Handle.handlets "
              "WHERE Parent.handleid=%llu AND Parent.handlets=%u", p_object_handle->id,
              p_object_handle->ts );

    st = db_exec_sql( p_conn, query, &res );
    if ( FSAL_POSIXDB_IS_ERROR( st ) )
        return st;

    /* entry not found */
    if ( mysql_num_rows( res ) == 0 )
    {
        mysql_free_result( res );
        ReturnCode( ERR_FSAL_POSIXDB_NOENT, 0 );
    }

    row = mysql_fetch_row( res );
    if ( row == NULL )
    {
        mysql_free_result( res );
        ReturnCode( ERR_FSAL_POSIXDB_FAULT, 0 );
    }

#ifdef _DEBUG_FSAL
    DisplayLog( "lookupPathsExt" );
#endif

    p_parent_directory_handle->id = atoll( row[1] );
    p_parent_directory_handle->ts = atoi( row[2] );
    posixdb_internal_fillFileinfoFromStrValues( &( p_parent_directory_handle->info ), row[3],
                                                row[4], row[5], row[6], row[7] );

    mysql_free_result( res );

    ReturnCode( ERR_FSAL_POSIXDB_NOERR, 0 );
}