/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright  held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is based on OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "swakCodedFunctionObject.H"
#include "volFields.H"
#include "dictionary.H"
#include "Time.H"
#include "SHA1Digest.H"
#include "dynamicCode.H"
#include "dynamicCodeContext.H"
#include "stringOps.H"
#include "addToRunTimeSelectionTable.H"

#include "GlobalVariablesRepository.H"


// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(swakCodedFunctionObject, 0);

    addToRunTimeSelectionTable
    (
        functionObject,
        swakCodedFunctionObject,
        dictionary
    );
}



// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::swakCodedFunctionObject::swakCodedFunctionObject
(
    const word& name,
    const Time& time,
    const dictionary& dict
)
:
    codedFunctionObject(name,time,dict),
    swakToCodedNamespaces_(
        dict.lookupOrDefault<wordList>(
            "swakToCodedNamespaces",
            wordList(0)
        )
    ),
    codedToSwakNamespace_(
        dict.lookupOrDefault<word>(
            "codedToSwakNamespace",
            word("")
        )
    ),
    codedToSwakVariables_(
        dict.lookupOrDefault<wordList>(
            "codedToSwakVariables",
            wordList(0)
        )
    ),
    verboseCode_(
        dict.lookupOrDefault<bool>("verboseCode",false)
    )
{
    if(
        (
            codedToSwakVariables_.size()>0
            &&
            codedToSwakNamespace_==""
        )
        ||
        (
            codedToSwakVariables_.size()==0
            &&
            codedToSwakNamespace_!=""
        )
    ) {
        FatalErrorIn("swakCodedFunctionObject::swakCodedFunctionObject")
            << "'codedToSwakVariables' (Value: " << codedToSwakVariables_ << ") "
                << "and 'codedToSwakNamespace' (Value: " << codedToSwakNamespace_
                << ") both have to be set (or none)"
                << endl
                << abort(FatalError);
    }
    read(dict_);
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::swakCodedFunctionObject::~swakCodedFunctionObject()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void Foam::swakCodedFunctionObject::injectSwakCode(const word &key,const string &pre,const string &post)
{
    entry *code=dict_.lookupEntryPtr(key,false,false);
    string codeValue;

    if(code) {
        codeValue=string(code->stream());
    }
    if(!dict_.found(key+"Original")) {
        // avoid inserting the stuff twice
        dict_.add(word(key+"Original"),codeValue);
    }
     
    // doesn't work because of length restriction in IStringStream
    //            dict_.set("code",codePrefix+codeValue+codePostfix);
    dict_.set(
        primitiveEntry(key,token(string(pre+codeValue+post)))
    );            
}


bool Foam::swakCodedFunctionObject::read(const dictionary& dict)
{
    //    bool success=codedFunctionObject::read(dict);
    //  Info << dict.lookup("code") << dict.lookup("code") << endl;
    string codePrefix="// inserted by swak - start\n";
    forAll(swakToCodedNamespaces_,nameI) {
        const word name(swakToCodedNamespaces_[nameI]);
        const GlobalVariablesRepository::ResultTable &vars=
            GlobalVariablesRepository::getGlobalVariables().getNamespace(
               name
            );
        if(verboseCode_) {
            codePrefix+="Info << \"Reading from Namespace " + name + "\" << endl;\n";
        }

        const word scopeName("swakGlobalNamespace_"+name);
        codePrefix+="GlobalVariablesRepository::ResultTable &"+scopeName
            +"=GlobalVariablesRepository::getGlobalVariables().getNamespace(\""
            +name+"\");\n";
        forAllConstIter(
            GlobalVariablesRepository::ResultTable,
            vars,
            iter
        ) {
            const ExpressionResult &val=(*iter);
            if(val.hasValue()) {
                if(val.isSingleValue()) {
                    codePrefix+=val.type()+" "+iter.key()
                        +"("+scopeName+"[\""+iter.key()+"\"].getResult<"
                        +val.type()+">(true)()[0]);\n";
                } else {
                    codePrefix+="Field<"+val.type()+"> "+iter.key()
                        +"("+scopeName+"[\""+iter.key()+"\"].getResult<"
                        +val.type()+">(true));\n";
                }
                if(verboseCode_) {
                    codePrefix+="Info << \"Got variable: " + iter.key() + ": \" << " 
                        + iter.key() + " << \" from \" << "
                        +scopeName+"[\""+iter.key()+"\"]" + "<< endl;\n";  
                }
            } else {
                codePrefix+="// Variable " + iter.key() + " has no value\n";
            }
        }
    }
    codePrefix+="// inserted by swak - end\n\n";

    string codePostfix="\n// inserted by swak - start\n";
    if(codedToSwakNamespace_!="") {
        forAll(codedToSwakVariables_,i) {
            word name=codedToSwakVariables_[i];
            codePostfix+="GlobalVariablesRepository::getGlobalVariables().";
            codePostfix+="addValue(\""+name+"\",\""+codedToSwakNamespace_+
                "\",ExpressionResult("+name+"));\n";
            if(verboseCode_ && 0) {
                codePostfix+="Info << \"Wrote " + name + " as: \" << "
                    +"GlobalVariablesRepository::getGlobalVariables()."
                    +"getNamespace(\""+codedToSwakNamespace_+"\")"
                    +"[\""+name+"\"] << endl;\n";
            }
        }
    }
    codePostfix+="// inserted by swak - end\n";

    // the stuff here only works because we assume that dict and dict_ are the same
    // which IMHO is implicitly assumed for the original codedFunctionObject
    injectSwakCode(
        "code",
        codePrefix,
        codePostfix
    );
    injectSwakCode(
        "codeInclude",
        "",
        "\n//swakStuff\n#include \"GlobalVariablesRepository.H\"\n"
    );
    
    if(!env("SWAK4FOAM_SRC")) {
        FatalErrorIn("swakCodedFunctionObject::read(const dictionary& dict)")
            << "Compilation of the function object only works if "
                << "the environment variable SWAK4FOAM_SRC points to the "
                << "Libraries directory of the swak4Foam-sources"
                << endl
                << abort(FatalError);
    }
    injectSwakCode(
        "codeOptions",
        "",
        " -I$(SWAK4FOAM_SRC)/swak4FoamParsers/lnInclude "
    );

    // the insertion of the #line breaks Make/options. Luckily we don't need this
//     injectSwakCode(
//         "codeLibs",
//         " -L$(FOAM_USER_LIBBIN) -lswak4FoamParsers ",
//         ""
//     );

    const entry* readPtr = dict.lookupEntryPtr
    (
        "codeRead",
        false,
        false
    );
    if (readPtr)
    {
        codeRead_ = stringOps::trim(codePrefix+string(readPtr->stream())+codePostfix);
        stringOps::inplaceExpand(codeRead_, dict);
        dynamicCodeContext::addLineDirective
        (
            codeRead_,
            readPtr->startLineNumber(),
            dict.name()
        );
    }

    const entry* execPtr = dict.lookupEntryPtr
    (
        "codeExecute",
        false,
        false
    );
    if (execPtr)
    {
        codeExecute_ = stringOps::trim(codePrefix+string(readPtr->stream())+codePostfix);
        stringOps::inplaceExpand(codeExecute_, dict);
        dynamicCodeContext::addLineDirective
        (
            codeExecute_,
            execPtr->startLineNumber(),
            dict.name()
        );
    }

    const entry* endPtr = dict.lookupEntryPtr
    (
        "codeEnd",
        false,
        false
    );
    if (execPtr)
    {
        codeEnd_ = stringOps::trim(codePrefix+string(readPtr->stream())+codePostfix);
        stringOps::inplaceExpand(codeEnd_, dict);
        dynamicCodeContext::addLineDirective
        (
            codeEnd_,
            endPtr->startLineNumber(),
            dict.name()
        );
    }

    updateLibrary();
    return redirectFunctionObject().read(dict);
}


// ************************************************************************* //