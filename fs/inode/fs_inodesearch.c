/****************************************************************************
 * fs/inode/fs_inodesearch.c
 *
 *   Copyright (C) 2007-2009, 2011-2012, 2016-2017 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <limits.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/fs/fs.h>

#include "inode/inode.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

FAR struct inode *g_root_inode = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: _inode_compare
 *
 * Description:
 *   Compare two inode names
 *
 ****************************************************************************/

static int _inode_compare(FAR const char *fname,
                          FAR struct inode *node)
{
  char *nname = node->i_name;

  if (!nname)
    {
      return 1;
    }

  if (!fname)
    {
      return -1;
    }

  for (; ; )
    {
      /* At end of node name? */

      if (!*nname)
        {
          /* Yes.. also end of find name? */

          if (!*fname || *fname == '/')
            {
              /* Yes.. return match */

              return 0;
            }
          else
            {
              /* No... return find name > node name */

              return 1;
            }
        }

      /* At end of find name? */

      else if (!*fname || *fname == '/')
        {
          /* Yes... return find name < node name */

          return -1;
        }

      /* Check for non-matching characters */

      else if (*fname > *nname)
        {
          return 1;
        }
      else if (*fname < *nname)
        {
          return -1;
        }

      /* Not at the end of either string and all of the
       * characters still match.  keep looking.
       */

      else
        {
          fname++;
          nname++;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: inode_search and inode_search_nofollow
 *
 * Description:
 *   Find the inode associated with 'path' returning the inode references
 *   and references to its companion nodes.
 *
 *   Both versions will follow soft links in path leading up to the terminal
 *   node.  inode_search() will deference that terminal node,
 *   inode_search_nofollow will not.
 *
 *   If a soft link is encountered that is not the terminal node in the path,
 *   that that WILL be deferenced and the mountpoint inode will be returned.
 *
 * Assumptions:
 *   The caller holds the g_inode_sem semaphore
 *
 ****************************************************************************/

#ifdef CONFIG_PSEUDOFS_SOFTLINKS

FAR struct inode *inode_search_nofollow(FAR const char **path,
                                        FAR struct inode **peer,
                                        FAR struct inode **parent,
                                        FAR const char **relpath)
#else
FAR struct inode *inode_search(FAR const char **path,
                               FAR struct inode **peer,
                               FAR struct inode **parent,
                               FAR const char **relpath)
#endif
{
  FAR const char   *name  = *path + 1; /* Skip over leading '/' */
  FAR struct inode *node  = g_root_inode;
  FAR struct inode *left  = NULL;
  FAR struct inode *above = NULL;
#ifdef CONFIG_PSEUDOFS_SOFTLINKS
  FAR struct inode *newnode;
#endif

  /* Traverse the pseudo file system node tree until either (1) all nodes
   * have been examined without finding the matching node, or (2) the
   * matching node is found.
   */

  while (node != NULL)
    {
      int result = _inode_compare(name, node);

      /* Case 1:  The name is less than the name of the node.
       * Since the names are ordered, these means that there
       * is no peer node with this name and that there can be
       * no match in the fileystem.
       */

      if (result < 0)
        {
          node = NULL;
          break;
        }

      /* Case 2: the name is greater than the name of the node.
       * In this case, the name may still be in the list to the
       * "right"
       */

      else if (result > 0)
        {
          /* Continue looking to the "right" of this inode. */

          left = node;
          node = node->i_peer;
        }

      /* The names match */

      else
        {
          /* Now there are three remaining possibilities:
           *   (1) This is the node that we are looking for.
           *   (2) The node we are looking for is "below" this one.
           *   (3) This node is a mountpoint and will absorb all request
           *       below this one
           */

          name = inode_nextname(name);
          if (*name == '\0' || INODE_IS_MOUNTPT(node))
            {
              /* Either (1) we are at the end of the path, so this must be the
               * node we are looking for or else (2) this node is a mountpoint
               * and will handle the remaining part of the pathname
               */

              if (relpath)
                {
                  *relpath = name;
                }

#ifdef CONFIG_PSEUDOFS_SOFTLINKS
              /* NOTE that if the terminal inode is a soft link, it is not
               * deferenced in this case. The raw inode is returned.
               *
               * In that case a wrapper function will perform that operation.
               */
#endif
              break;
            }
          else
            {
              /* More nodes to be examined in the path "below" this one. */

#ifdef CONFIG_PSEUDOFS_SOFTLINKS
              /* If this intermediate inode in the is a soft link, then (1)
               * get the name of the full path of the soft link, (2) recursively
               * look-up the inode referenced by the sof link, and (3)
               * continue searching with that inode instead.
               */

              newnode = inode_linktarget(node,  NULL, NULL, relpath);
              if (newnode == NULL)
                {
                  /* Probably means that the node is a symbolic link, but
                   * that the target of the symbolic link does not exist.
                   */

                  break;
                }
              else if (newnode != node)
                {
                  /* The node was a valid symbolic link and we have jumped to a
                   * different, spot in the the pseudo file system tree.
                   *
                   * Continue from this new inode.
                   */

                  node = newnode;

                  /* Check if this took us to a mountpoint. */

                  if (INODE_IS_MOUNTPT(newnode))
                    {
                      /* Yes.. return the mountpoint information.
                       * REVISIT: The relpath is incorrect in this case.  It is
                       * relative to symbolic link, not to the root of the mount.
                       */

                      if (relpath)
                       {
                         *relpath = name;
                       }

                      above = NULL;
                      left  = NULL;
                      break;
                    }
                }
#endif
              /* Keep looking at the next level "down" */

              above = node;
              left  = NULL;
              node  = node->i_child;
            }
        }
    }

  /* node is null.  This can happen in one of four cases:
   * With node = NULL
   *   (1) We went left past the final peer:  The new node
   *       name is larger than any existing node name at
   *       that level.
   *   (2) We broke out in the middle of the list of peers
   *       because the name was not found in the ordered
   *       list.
   *   (3) We went down past the final parent:  The new node
   *       name is "deeper" than anything that we currently
   *       have in the tree.
   * with node != NULL
   *   (4) When the node matching the full path is found
   */

  if (peer)
    {
      *peer = left;
    }

  if (parent)
    {
      *parent = above;
    }

  *path = name;
  return node;
}

#ifdef CONFIG_PSEUDOFS_SOFTLINKS
FAR struct inode *inode_search(FAR const char **path,
                               FAR struct inode **peer,
                               FAR struct inode **parent,
                               FAR const char **relpath)
{
  /* Lookup the terminal inode */

  FAR struct inode *node = inode_search_nofollow(path, peer, parent, relpath);

  /* Did we find it?  inode_search_nofollow() will terminate in one of
   * three ways:
   *
   *   1. With an error (node == NULL)
   *   2. With node referring to the terminal inode which may be a symbolic
   *      link, or
   *   3. with node referring to an intermediate MOUNTPOINT inode with the
   *      residual path in relpath.
   *
   * REVISIT: The relpath is incorrect in the final case.  It will be relative
   * to symbolic link, not to the root of the mount.
   */

  if (node != NULL && INODE_IS_SOFTLINK(node))
    {
      /* The terminating inode is a valid soft link:  Return the inode,
       * corresponding to link target.
       */

       return inode_linktarget(node, peer, parent, relpath);
    }

  return node;
}
#endif

/****************************************************************************
 * Name: inode_linktarget
 *
 * Description:
 *   If the inode is a soft link, then (1) get the name of the full path of
 *   the soft link, (2) recursively look-up the inode referenced by the soft
 *   link, and (3) return the inode referenced by the soft link.
 *
 * Assumptions:
 *   The caller holds the g_inode_sem semaphore
 *
 ****************************************************************************/

#ifdef CONFIG_PSEUDOFS_SOFTLINKS
FAR struct inode *inode_linktarget(FAR struct inode *node,
                                   FAR struct inode **peer,
                                   FAR struct inode **parent,
                                   FAR const char **relpath)
{
  FAR const char *copy;
  unsigned int count = 0;

  /* An infinite loop is avoided only by the loop count.
  *
   * REVISIT:  The ELOOP error should be reported to the application in that
   * case but there is no simple mechanism to do that.
   */

  while (node != NULL && INODE_IS_SOFTLINK(node))
    {
      /* Careful: inode_search_nofollow overwrites the input string pointer */

      copy = (FAR const char *)node->u.i_link;

      /* Now, look-up the inode associated with the target path */

      node = inode_search_nofollow(&copy, peer, parent, relpath);
      if (node == NULL && ++count > SYMLOOP_MAX)
        {
          return NULL;
        }
    }

  return node;
}
#endif

/****************************************************************************
 * Name: inode_nextname
 *
 * Description:
 *   Given a path with node names separated by '/', return the next path
 *   segment name.
 *
 ****************************************************************************/

FAR const char *inode_nextname(FAR const char *name)
{
  /* Search for the '/' delimiter or the NUL terminator at the end of the
   * path segment.
   */

  while (*name && *name != '/')
    {
      name++;
    }

  /* If we found the '/' delimiter, then the path segment we want begins at
   * the next character (which might also be the NUL terminator).
   */

  if (*name)
    {
      name++;
    }

  return name;
}