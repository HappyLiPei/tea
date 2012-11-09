#shamelessly taken from package flexmix
KLdivM <- function(object, eps=10^-4, overlap=TRUE,...)
{
    if(!is.numeric(object))
        stop("object must be a numeric matrix\n")

    z <- matrix(NA, nrow=ncol(object), ncol=ncol(object))
    colnames(z) <- rownames(z) <- colnames(object)

    w <- object < eps
    if (any(w)) object[w] <- eps
    object <- sweep(object, 2, colSums(object) , "/")

    for(k in seq_len(ncol(object)-1)){
      for(l in 2:ncol(object)){
        ok <- (object[, k] > eps) & (object[, l] > eps)
        if (!overlap | any(ok)) {
          z[k,l] <- sum(object[,k] *
                        (log(object[,k]) - log(object[,l])))
          z[l,k] <- sum(object[,l] *
                        (log(object[,l]) - log(object[,k])))
        }
      }
    }
    diag(z)<-0
    return(z)
}

tea.klcoda <- function(M1,M2){
	if(!identical(dim(M1),dim(M2))) stop("klcoda requires matrices of same dimension")
	M1 <- apply(M1,2,function(x) return(ecdf(x)(x)))
	M2 <- apply(M2,2,function(x) return(ecdf(x)(x)))
	A <- array(rbind(M1,M2),c(nrow(M1),2,ncol(M1)))
	Mkl <- apply(A,3,function(x) return(KLdivM(x)))
	#each column of Mkl is the result of KLdiv for that column of M1/M2
	return(Mkl)
}

#env
#	LHS = formula containing LHS variables in desired imputation order (FIFO)
#	RHS = formula of predictor variables
#	Data = data with missing items to impute; if no missing data are found, stops
#	del = convergence criterion; sum((th1-th0)^2) < del results in convergence
tea.srmi.est <- function(env){
	if(env$debug>0) browser()
	#x and y variables
	vyvar <- attr(terms(env$LHS),"term.labels")
	vxvar <- attr(terms(env$RHS),"term.labels")

	#check for things
	if(sum(unlist(lapply(env$Data[vxvar],is.na)))>0)
		stop("X variables can have no missing values")

	#factorize characters
	env$Data <- TEAConformDF(env$Data,env$Data)

	#NAs for LHS
	lna <- lapply(env$Data[vyvar],function(x) return(which(is.na(x))))

	#y types
	vchar <- unlist(lapply(env$Data[vyvar],is.character))
	vfact <- unlist(lapply(env$Data[vyvar],is.factor))
	vcat <- vchar|vfact
	vnum <- unlist(lapply(env$Data[vyvar],is.numeric))

	#apop models we'll need
	modreg <- setupRapopModel(mcmc.reg)
	modmnl <- setupRapopModel(mcmc.mnl)
	
	lfit <- list()
	env$Newdata <- env$Data

	#first round of fits/draws
	for(idx in 1:length(vyvar)){
		kvar <- vyvar[idx]
		formmod <- as.formula(paste(kvar,
			paste(c(vxvar,vyvar[-(idx:length(vyvar))]),collapse="+"),sep="~"))
		print(formmod)
		emod <- as.environment(list(Data=env$Newdata[-lna[[kvar]],],Formula=formmod))
		if(vcat[idx]) lfit[[kvar]] <- RapopModelEstimate(emod,modmnl)
		else lfit[[kvar]] <- RapopModelEstimate(emod,modreg)
		#set newdata in fit to missing vals
		lfit[[kvar]]$env$Newdata <- env$Newdata[lna[[kvar]],]
		if(env$debug>1) lfit[[kvar]]$env$debug <- 1
		#predict data for missing values
		dfs <- RapopModelDraw(lfit[[kvar]])
		#replace old data
		env$Newdata[lna[[kvar]],kvar] <- dfs[,kvar]
	}

	#second round of fits/draws
	for(hdx in 1:env$maxit){
		for(idx in 1:length(vyvar)){
			if(hdx>2) Mkl0 <- Mkl1
			kvar <- vyvar[idx]
			formmod <- as.formula(paste(kvar,
				paste(c(vxvar,vyvar[-idx]),collapse="+"),sep="~"))
			print(paste(hdx,formmod,sep=": "))
			M1 <- lfit[[kvar]]$env$Fit
			emod <- as.environment(list(Data=env$Newdata,Formula=formmod))
			if(vcat[idx]) lfit[[kvar]] <- RapopModelEstimate(emod,modmnl)
			else lfit[[kvar]] <- RapopModelEstimate(emod,modreg)
			M2 <- lfit[[kvar]]$env$Fit
			#currently not doing anything with the KL div,
			#but we could use it for a stopping criterion
			if(hdx>1) Mkl1 <- tea.klcoda(M1,M2)
			if(hdx>2) vkldiff1 <- (sum((colSums(Mkl0)-colSums(Mkl1))^2))
			#predict data for missing values
			lfit[[kvar]]$env$Newdata <- env$Newdata[lna[[kvar]],]
			dfs <- RapopModelDraw(lfit[[kvar]])
			#replace old data
			env$Newdata[lna[[kvar]],kvar] <- dfs[,kvar]
		}
	}

	env$Lfit <- lfit #save fits
	env$Lna <- lna
	return(NULL)
}

tea.srmi.draw <- function(env){
	if(env$debug>0) browser()
	#factorize characters
	#env$Newdata <- TEAConformDF(env$Newdata,env$Newdata)

	#NAs for LHS
	#lna <- lapply(env$Newdata[names(env$Lfit)],function(x) return(which(is.na(x))))

	for(idx in 1:length(env$Lfit)){
		kvar <- names(env$Lfit)[[idx]]
		#predict data for missing values
		env$Lfit[[kvar]]$env$Newdata <- env$Newdata[env$Lna[[kvar]],]
		env$Lfit[[kvar]]$env$draw <- "missing"
		dfs <- RapopModelDraw(env$Lfit[[kvar]])
		#replace old data
		env$Newdata[env$Lna[[kvar]],kvar] <- dfs[,kvar]
	}
	return(env$Newdata)
}

tea.srmi <- new("apop_model", name="srmi",  
                                estimate_function=tea.srmi.est,
                                draw_function=tea.srmi.draw)