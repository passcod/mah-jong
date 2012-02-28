/* $Header: /home/jcb/MahJong/newmj/RCS/vlazyfixed.h,v 12.0 2009/06/28 20:43:31 jcb Rel $
 * vlazyfixed.h
 * Another slight variation on the GTK+ fixed widget.
 * It doesn't call queue_resize when children are removed, added,
 * or move, EXCEPT when the first child is added.
 * This has the consequence that move DOESN'T work, unles you
 * explicitly call resize. But that's OK, since we only use this
 * in a situation where we can physically move the child window.
 */
/****************** COPYRIGHT STATEMENT **********************
 * This file is Copyright (c) 2001 by J. C. Bradfield.       *
 * This file may be used under the terms of the              *
 * GNU Lesser General Public License (any version).           *
 * The moral rights of the author are asserted.              *
 *                                                           *
 ***************** DISCLAIMER OF WARRANTY ********************
 * This code is not warranted fit for any purpose. See the   *
 * LICENCE file for further information.                     *
 *                                                           *
 *************************************************************/


#ifndef __VLAZY_FIXED_H__
#define __VLAZY_FIXED_H__


#include <gtk/gtkfixed.h>
#include "sysdep.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define VLAZY_FIXED(obj)                  (GTK_CHECK_CAST ((obj), vlazy_fixed_get_type(), VlazyFixed))
#define VLAZY_FIXED_CLASS(klass)          (GTK_CHECK_CLASS_CAST ((klass), vlazy_fixed_get_type(), VlazyFixedClass))
#define IS_VLAZY_FIXED(obj)               (GTK_CHECK_TYPE ((obj), vlazy_fixed_get_type()))

typedef struct _VlazyFixed        VlazyFixed;
typedef struct _VlazyFixedClass   VlazyFixedClass;

struct _VlazyFixed
{
  GtkFixed fixed;
};

struct _VlazyFixedClass
{
  GtkFixedClass parent_class;
};


GtkType    vlazy_fixed_get_type          (void);
GtkWidget* vlazy_fixed_new               (void);
void       vlazy_fixed_put               (VlazyFixed       *fixed,
                                        GtkWidget      *widget,
                                        gint16         x,
                                        gint16         y);
void       vlazy_fixed_move              (VlazyFixed       *fixed,
                                        GtkWidget      *widget,
                                        gint16         x,
                                        gint16         y);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __VLAZY_FIXED_H__ */
