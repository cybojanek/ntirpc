/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est r�gi par la licence CeCILL soumise au droit fran�ais et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffus�e par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilit� au code source et des droits de copie,
 * de modification et de redistribution accord�s par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limit�e.  Pour les m�mes raisons,
 * seule une responsabilit� restreinte p�se sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les conc�dants successifs.
 *
 * A cet �gard  l'attention de l'utilisateur est attir�e sur les risques
 * associ�s au chargement,  � l'utilisation,  � la modification et/ou au
 * d�veloppement et � la reproduction du logiciel par l'utilisateur �tant
 * donn� sa sp�cificit� de logiciel libre, qui peut le rendre complexe �
 * manipuler et qui le r�serve donc � des d�veloppeurs et des professionnels
 * avertis poss�dant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invit�s � charger  et  tester  l'ad�quation  du
 * logiciel � leurs besoins dans des conditions permettant d'assurer la
 * s�curit� de leurs syst�mes et ou de leurs donn�es et, plus g�n�ralement,
 * � l'utiliser et l'exploiter dans les m�mes conditions de s�curit�.
 *
 * Le fait que vous puissiez acc�der � cet en-t�te signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accept� les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */

/**
 * \file    cache_inode_setattr.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/14 11:47:40 $
 * \version $Revision: 1.19 $
 * \brief   Sets the attributes for an entry.
 *
 * cache_inode_setattr.c : Sets the attributes for an entry.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_setattrs: set the attributes for an entry located in the cache by its address. 
 * 
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided 
 * with compliance to the underlying FSAL semantics. Attributes that are set are returned in "*pattr".
 *
 * @param pentry_parent [IN] entry for the parent directory to be managed.
 * @param pattr [INOUT] attributes for the entry that we have found. Out: attributes set.
 * @param ht [INOUT] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_setattr(cache_entry_t * pentry, fsal_attrib_list_t * pattr, hash_table_t * ht, /* Unused, kept for protototype's homogeneity */
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle;
  fsal_status_t fsal_status;
  fsal_attrib_list_t *p_object_attributes;
  fsal_attrib_list_t result_attributes;
  fsal_attrib_list_t truncate_attributes;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_SETATTR] += 1;

  /* Lock the entry */
  P_w(&pentry->lock);

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      pfsal_handle = &pentry->object.file.handle;
      break;

    case SYMBOLIC_LINK:
      pfsal_handle = &pentry->object.symlink.handle;
      break;

    case DIR_BEGINNING:
      pfsal_handle = &pentry->object.dir_begin.handle;
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P_r(&pentry->object.dir_cont.pdir_begin->lock);
      pfsal_handle = &pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V_r(&pentry->object.dir_cont.pdir_begin->lock);
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      pfsal_handle = &pentry->object.special_obj.handle;
      break;
    }

  /* Call FSAL to set the attributes */
  /* result_attributes.asked_attributes = pattr->asked_attributes ; */

  /* mod Th.Leibovici on 2006/02/13
   * We ask back all standard attributes, in case they have been modified
   * by another program (pftp, rcpd...)
   */
  result_attributes.asked_attributes = pclient->attrmask;
  /* end of mod */

#ifdef _USE_MFSL
  fsal_status =
      MFSL_setattrs(&pentry->mobject, pcontext, &pclient->mfsl_context, pattr,
                    &result_attributes);
#else
  cache_inode_get_attributes(pentry, &result_attributes);

  fsal_status = FSAL_setattrs(pfsal_handle, pcontext, pattr, &result_attributes);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V_w(&pentry->lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          DisplayLog
              ("cache_inode_setattr: Stale FSAL File Handle detected for pentry = %p",
               pentry);

          if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            DisplayLog("cache_inode_setattr: Could not kill entry %p, status = %u",
                       pentry, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }

      return *pstatus;
    }

  if(pattr->asked_attributes & FSAL_ATTR_SIZE)
    {
      truncate_attributes.asked_attributes = pclient->attrmask;

      fsal_status = FSAL_truncate(pfsal_handle,
                                  pcontext, pattr->filesize, NULL, &truncate_attributes);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          V_w(&pentry->lock);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              DisplayLog
                  ("cache_inode_setattr: Stale FSAL File Handle detected for pentry = %p",
                   pentry);

              if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                DisplayLog("cache_inode_setattr: Could not kill entry %p, status = %u",
                           pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          return *pstatus;
        }

    }

  /* Keep the new attribute in cache */
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      p_object_attributes = &(pentry->object.file.attributes);
      break;

    case SYMBOLIC_LINK:
      p_object_attributes = &(pentry->object.symlink.attributes);
      break;

    case DIR_BEGINNING:
      p_object_attributes = &(pentry->object.dir_begin.attributes);
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P_r(&pentry->object.dir_cont.pdir_begin->lock);
      p_object_attributes =
          &(pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes);
      V_r(&pentry->object.dir_cont.pdir_begin->lock);
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      p_object_attributes = &(pentry->object.special_obj.attributes);
      break;

    }

  /* Update the cached attributes */
  if((result_attributes.asked_attributes & FSAL_ATTR_SIZE) ||
     (result_attributes.asked_attributes & FSAL_ATTR_SPACEUSED))
    {

      if(pentry->internal_md.type == REGULAR_FILE)
        {
          if(pentry->object.file.pentry_content == NULL)
            {
              /* Operation on a non data cached file */
              p_object_attributes->filesize = result_attributes.filesize;
              p_object_attributes->spaceused = result_attributes.filesize;      /* Unclear hook here. BUGAZOMEU */
            }
          else
            {
              /* Data cached file */
              /* Do not set the p_object_attributes->filesize and p_object_attributes->spaceused  in this case 
               * This will lead to a situation where (for example) untar-ing a file will produced invalid files 
               * with a size of 0 despite the fact that they are not empty */
#ifdef  _DEBUG_CACHE_INODE
              DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                                "cache_inode_setattr with FSAL_ATTR_SIZE on data cached entry");
#endif
            }
        }
      else if(pattr->asked_attributes & FSAL_ATTR_SIZE)
        DisplayLog
            ("WARNING !!! cache_inode_setattr tryed to operate size on a non REGULAR_FILE type=%d",
             pentry->internal_md.type);
    }

  if(result_attributes.asked_attributes &
     (FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(result_attributes.asked_attributes & FSAL_ATTR_MODE)
        p_object_attributes->mode = result_attributes.mode;

      if(result_attributes.asked_attributes & FSAL_ATTR_OWNER)
        p_object_attributes->owner = result_attributes.owner;

      if(result_attributes.asked_attributes & FSAL_ATTR_GROUP)
        p_object_attributes->group = result_attributes.group;
    }

  if(result_attributes.asked_attributes &
     (FSAL_ATTR_ATIME | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
    {
      if(result_attributes.asked_attributes & FSAL_ATTR_ATIME)
        p_object_attributes->atime = result_attributes.atime;

      if(result_attributes.asked_attributes & FSAL_ATTR_CTIME)
        p_object_attributes->ctime = result_attributes.ctime;

      if(result_attributes.asked_attributes & FSAL_ATTR_MTIME)
        p_object_attributes->mtime = result_attributes.mtime;
    }

  /* Return the attributes as set */
  *pattr = *p_object_attributes;

  /* validate the entry */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Release the entry */
  V_w(&pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_SETATTR] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_SETATTR] += 1;

  return *pstatus;
}                               /* cache_inode_setattr */