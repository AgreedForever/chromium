// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.printing;

import android.print.PrintDocumentAdapter;

/**
 * This interface describes a class which is responsible of talking to the printing backend.
 *
 * Such class communicates with a {@link PrintingContext}, which in turn talks to the native side.
 */
public interface PrintingController {
    /**
     * @return Dots Per Inch (DPI) of the currently selected printer.
     */
    int getDpi();

    /**
     * @return The file descriptor number of the file into which Chromium will write the PDF.  This
     *         is provided to us by {@link PrintDocumentAdapter#onWrite}.
     */
    int getFileDescriptor();

    /**
     * @return The media height in mils (thousands of an inch).
     */
    int getPageHeight();

    /**
     * @return The media width in mils (thousands of an inch).
     */
    int getPageWidth();

    /**
     * @return The individual page numbers of the document to be printed, of null if all pages are
     *         to be printed.  The numbers are zero indexed.
     */
    int[] getPageNumbers();

    /**
     * @return If the controller is busy.
     */
    public boolean isBusy();

    /**
     * Initiates the printing process for the Android API.
     *
     * @param printable An object capable of starting native side PDF generation, i.e. typically
     *                  a Tab.
     * @param printManager The print manager that manages the print job.
     */
    void startPrint(final Printable printable, PrintManagerDelegate printManager);

    /**
     * This method is called by the native side to signal PDF writing process is completed.
     *
     * @param success Whether the PDF is written into the provided file descriptor successfully.
     */
    void pdfWritingDone(boolean success);

    /**
     * Sets PrintingContext currently associated with the controller.
     *
     * This needs to be called after PrintingContext object is created. Firstly its native
     * counterpart is created, and then the Java. PrintingController implementation
     * needs this to interact with the native side, since JNI is built on PrintingContext.
     **/
    void setPrintingContext(final PrintingContextInterface printingContext);

    /**
     * @return Whether a complete PDF generation cycle inside Chromium has been completed.
     */
    boolean hasPrintingFinished();

    /**
     * Sets the data required to initiate a printing process. The process can later be started using
     * {@link #startPendingPrint(PrintingContextInterface)}.
     *
     * @param printable An object capable of starting native side PDF generation, i.e. typically
     *     a Tab.
     * @param printManager The print manager that manages the print job.
     */
    void setPendingPrint(final Printable printable, final PrintManagerDelegate printManager);

    /**
     * Starts printing, provided that the current object already has sufficient data to start the
     * process. (using {@link #setPendingPrint(Printable, PrintManagerDelegate)} for example)
     *
     * @param jsOriginatedPrintingContext The printingContext holding the callback to be used to
     *     reply when javascript can resume. When printing is done (or could not start),
     *     {@link PrintingContextInterface#showSystemDialogDone()} will be called on this object.
     */
    void startPendingPrint(PrintingContextInterface jsOriginatedPrintingContext);

}
